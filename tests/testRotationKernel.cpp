#include "spacial/tbfmortonspaceindex.hpp"
#include "spacial/tbfspacialconfiguration.hpp"
#include "utils/tbfrandom.hpp"
#include "core/tbfcellscontainer.hpp"
#include "core/tbfparticlescontainer.hpp"
#include "core/tbfparticlesorter.hpp"
#include "core/tbftree.hpp"
#include "algorithms/tbfalgorithmselecter.hpp"
#include "utils/tbftimer.hpp"

#include "kernels/rotationkernel/FRotationKernel.hpp"
#include "loader/tbffmaloader.hpp"
#include "utils/tbfaccuracychecker.hpp"

#include <iostream>


int main(int argc, char** argv){
    using RealType = double;
    const int Dim = 3;

    /////////////////////////////////////////////////////////////////////////////////////////

    std::vector<std::array<RealType, Dim+1>> particlePositions;
    long int nbParticles;
    std::array<RealType, Dim> BoxWidths;
    std::array<RealType, Dim> BoxCenter;

    if(argc == 2 && std::string(argv[1]) != "--help"){
        TbfFmaLoader<RealType, Dim, Dim+1> loader(argv[1]);

        if(!loader.isOpen()){
            std::cout << "[Error] There is a problem, the given file '" << argv[1] << "' cannot be open." << std::endl;
            return -1;
        }

        nbParticles = loader.getNbParticles();
        particlePositions = loader.loadAllParticles();
        BoxWidths = loader.getBoxWidths();
        BoxCenter = loader.getBoxCenter();
    }
    else if(argc == 1){
        BoxWidths = std::array<RealType, Dim>{{1, 1, 1}};
        BoxCenter = std::array<RealType, Dim>{{0.5, 0.5, 0.5}};

        nbParticles = 1000;

        TbfRandom<RealType, Dim> randomGenerator(BoxWidths);

        particlePositions.resize(nbParticles);

        for(long int idxPart = 0 ; idxPart < nbParticles ; ++idxPart){
            auto position = randomGenerator.getNewItem();
            particlePositions[idxPart][0] = position[0];
            particlePositions[idxPart][1] = position[1];
            particlePositions[idxPart][2] = position[2];
            particlePositions[idxPart][3] = 0.1;
        }
    }
    else{
        std::cout << "Only one parameter can be given, and it should be a FMA file." << std::endl;
        return -1;
    }

    const long int TreeHeight = 4;
    const TbfSpacialConfiguration<RealType, Dim> configuration(TreeHeight, BoxWidths, BoxCenter);

    std::cout << configuration << std::endl;

    /////////////////////////////////////////////////////////////////////////////////////////

    std::cout << "Particles info" << std::endl;
    std::cout << " - Number of particles = " << nbParticles << std::endl;
    std::cout << " - Here are the first particles..." << std::endl;
    for(long int idxPart = 0 ; idxPart < std::min(5L, nbParticles) ; ++idxPart){
        std::cout << TbfUtils::ArrayPrinter(particlePositions[idxPart]) << std::endl;
    }
    std::cout << std::endl;

    /////////////////////////////////////////////////////////////////////////////////////////

    const unsigned int P = 12;
    using ParticleDataType = RealType;
    constexpr long int NbDataValuesPerParticle = Dim+1;
    using ParticleRhsType = RealType;
    constexpr long int NbRhsValuesPerParticle = 4;

    constexpr long int VectorSize = ((P+2)*(P+1))/2;

    using MultipoleClass = std::array<std::complex<RealType>, VectorSize>;
    using LocalClass = std::array<std::complex<RealType>, VectorSize>;

    using KernelClass = FRotationKernel<RealType, P>;
    const long int NbElementsPerBlock = 50;
    const bool OneGroupPerParent = false;
    using AlgorithmClass = TbfAlgorithmSelecter::type<RealType, KernelClass>;
    using TreeClass = TbfTree<RealType,
                              ParticleDataType,
                              NbDataValuesPerParticle,
                              ParticleRhsType,
                              NbRhsValuesPerParticle,
                              MultipoleClass,
                              LocalClass>;

    /////////////////////////////////////////////////////////////////////////////////////////

    TbfTimer timerBuildTree;

    TreeClass tree(configuration, NbElementsPerBlock, TbfUtils::make_const(particlePositions), OneGroupPerParent);

    timerBuildTree.stop();
    std::cout << "Build the tree in " << timerBuildTree.getElapsed() << std::endl;

    /////////////////////////////////////////////////////////////////////////////////////////

    // Here we put the kernel in the heap to make sure not to overflow the stack
    std::unique_ptr<AlgorithmClass> algorithm(new AlgorithmClass(configuration));

    {
        TbfTimer timerExecute;

        algorithm->execute(tree);

        timerExecute.stop();
        std::cout << "Execute in " << timerExecute.getElapsed() << "s" << std::endl;
    }

    /////////////////////////////////////////////////////////////////////////////////////////

    {
        std::array<RealType*, 4> particles;
        for(auto& vec : particles){
            vec = new RealType[nbParticles]();
        }
        std::array<RealType*, NbRhsValuesPerParticle> particlesRhs;
        for(auto& vec : particlesRhs){
            vec = new RealType[nbParticles]();
        }

        for(long int idxPart = 0 ; idxPart < nbParticles ; ++idxPart){
            particles[0][idxPart] = particlePositions[idxPart][0];
            particles[1][idxPart] = particlePositions[idxPart][1];
            particles[2][idxPart] = particlePositions[idxPart][2];
            particles[3][idxPart] = particlePositions[idxPart][3];
        }

        TbfTimer timerDirect;

        FP2PR::template GenericInner<RealType>( particles, particlesRhs, nbParticles);

        timerDirect.stop();

        std::cout << "Direct execute in " << timerDirect.getElapsed() << "s" << std::endl;

        //////////////////////////////////////////////////////////////////////

        std::array<TbfAccuracyChecker<RealType>, 4> partcilesAccuracy;
        std::array<TbfAccuracyChecker<RealType>, NbRhsValuesPerParticle> partcilesRhsAccuracy;

        tree.applyToAllLeaves([&particles,&partcilesAccuracy,&particlesRhs,&partcilesRhsAccuracy]
                              (auto&& leafHeader, const long int* particleIndexes,
                              const std::array<RealType*, 4> particleDataPtr,
                              const std::array<RealType*, NbRhsValuesPerParticle> particleRhsPtr){
            for(int idxPart = 0 ; idxPart < leafHeader.nbParticles ; ++idxPart){
                for(int idxValue = 0 ; idxValue < 4 ; ++idxValue){
                   partcilesAccuracy[idxValue].addValues(particles[idxValue][particleIndexes[idxPart]],
                                                        particleDataPtr[idxValue][idxPart]);
                }
                for(int idxValue = 0 ; idxValue < NbRhsValuesPerParticle ; ++idxValue){
                   partcilesRhsAccuracy[idxValue].addValues(particlesRhs[idxValue][particleIndexes[idxPart]],
                                                        particleRhsPtr[idxValue][idxPart]);
                }
            }
        });

        std::cout << "Relative differences:" << std::endl;
        for(int idxValue = 0 ; idxValue < 4 ; ++idxValue){
           std::cout << " - Data " << idxValue << " = " << partcilesAccuracy[idxValue] << std::endl;
        }
        for(int idxValue = 0 ; idxValue < 4 ; ++idxValue){
           std::cout << " - Rhs " << idxValue << " = " << partcilesRhsAccuracy[idxValue] << std::endl;
        }

        //////////////////////////////////////////////////////////////////////

        for(auto& vec : particles){
            delete[] vec;
        }
        for(auto& vec : particlesRhs){
            delete[] vec;
        }
    }

    //////////////////////////////////////////////////////////////////////

    for(long int idxLoop = 0 ; idxLoop < 10 ; ++idxLoop){
        std::cout << " -- Loop " << idxLoop << std::endl;

        TbfTimer timerRebuildTree;

        tree.rebuild();

        timerRebuildTree.stop();
        std::cout << "Re-Build the tree in " << timerRebuildTree.getElapsed() << std::endl;

        {
            TbfTimer timerExecute;

            algorithm->execute(tree);

            timerExecute.stop();
            std::cout << "Execute in " << timerExecute.getElapsed() << "s" << std::endl;
        }
    }

    return 0;
}

