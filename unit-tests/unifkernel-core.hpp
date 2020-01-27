#ifndef TESTKERNEL_CORE_HPP
#define TESTKERNEL_CORE_HPP

#include "UTester.hpp"

#include "spacial/tbfmortonspaceindex.hpp"
#include "spacial/tbfspacialconfiguration.hpp"
#include "utils/tbfrandom.hpp"
#include "core/tbfcellscontainer.hpp"
#include "core/tbfparticlescontainer.hpp"
#include "core/tbfparticlesorter.hpp"
#include "core/tbftree.hpp"
#include "kernels/unifkernel/FUnifKernel.hpp"
#include "algorithms/tbfalgorithmutils.hpp"
#include "utils/tbftimer.hpp"


template <class RealType, template <typename T1, typename T2> class AlgorithmClass>
class TestUnifKernel : public UTester< TestUnifKernel<RealType, AlgorithmClass> > {
    using Parent = UTester< TestUnifKernel<RealType, AlgorithmClass> >;

    void CorePart(const long int NbParticles, const long int inNbElementsPerBlock,
                  const bool inOneGroupPerParent, const long int TreeHeight){
        const int Dim = 3;

        /////////////////////////////////////////////////////////////////////////////////////////

        const std::array<RealType, Dim> BoxWidths{{1, 1, 1}};
        const std::array<RealType, Dim> inBoxCenter{{0.5, 0.5, 0.5}};

        const TbfSpacialConfiguration<RealType, Dim> configuration(TreeHeight, BoxWidths, inBoxCenter);

        /////////////////////////////////////////////////////////////////////////////////////////

        TbfRandom<RealType, Dim> randomGenerator(configuration.getBoxWidths());

        std::vector<std::array<RealType, Dim+1>> particlePositions(NbParticles);

        for(long int idxPart = 0 ; idxPart < NbParticles ; ++idxPart){
            auto pos = randomGenerator.getNewItem();
            particlePositions[idxPart][0] = pos[0];
            particlePositions[idxPart][1] = pos[1];
            particlePositions[idxPart][2] = pos[2];
            particlePositions[idxPart][3] = RealType(0.01);
        }

        /////////////////////////////////////////////////////////////////////////////////////////

        const unsigned int ORDER = 8;
        constexpr long int NbDataValuesPerParticle = Dim+1;
        constexpr long int NbRhsValuesPerParticle = 4;

        constexpr long int VectorSize = TensorTraits<ORDER>::nnodes;
        constexpr long int TransformedVectorSize = (2*ORDER-1)*(2*ORDER-1)*(2*ORDER-1);

        struct MultipoleData{
            RealType multipole_exp[VectorSize];
            std::complex<RealType> transformed_multipole_exp[TransformedVectorSize];
        };

        struct LocalData{
            RealType     local_exp[VectorSize];
            std::complex<RealType>     transformed_local_exp[TransformedVectorSize];
        };

        using MultipoleClass = MultipoleData;
        using LocalClass = LocalData;
        using KernelClass = FUnifKernel<RealType, FInterpMatrixKernelR<RealType>, ORDER>;

        /////////////////////////////////////////////////////////////////////////////////////////

        TbfTimer timerBuildTree;

        TbfTree<RealType, RealType, NbDataValuesPerParticle, RealType, NbRhsValuesPerParticle, MultipoleClass, LocalClass> tree(configuration, inNbElementsPerBlock,
                                                                                    TbfUtils::make_const(particlePositions), inOneGroupPerParent);

        timerBuildTree.stop();
        std::cout << "Build the tree in " << timerBuildTree.getElapsed() << std::endl;

        FInterpMatrixKernelR<RealType> interpolator;
        AlgorithmClass<RealType, KernelClass> algorithm(configuration, KernelClass(configuration, &interpolator));

        TbfTimer timerExecute;

        algorithm.execute(tree);

        timerExecute.stop();
        std::cout << "Execute in " << timerExecute.getElapsed() << "s" << std::endl;

        /////////////////////////////////////////////////////////////////////////////////////////

        {
            std::array<RealType*, 4> particles;
            for(auto& vec : particles){
                vec = new RealType[NbParticles]();
            }
            std::array<RealType*, NbRhsValuesPerParticle> particlesRhs;
            for(auto& vec : particlesRhs){
                vec = new RealType[NbParticles]();
            }

            for(long int idxPart = 0 ; idxPart < NbParticles ; ++idxPart){
                particles[0][idxPart] = particlePositions[idxPart][0];
                particles[1][idxPart] = particlePositions[idxPart][1];
                particles[2][idxPart] = particlePositions[idxPart][2];
                particles[3][idxPart] = particlePositions[idxPart][3];
            }

            TbfTimer timerDirect;

            FP2PR::template GenericInner<RealType>( particles, particlesRhs, NbParticles);

            timerDirect.stop();

            std::cout << "Direct execute in " << timerDirect.getElapsed() << "s" << std::endl;

            //////////////////////////////////////////////////////////////////////

            std::array<RealType, 4> partcilesAccuracy;
            std::array<RealType, NbRhsValuesPerParticle> partcilesRhsAccuracy;

            tree.applyToAllLeaves([&particles,&partcilesAccuracy,&particlesRhs,&partcilesRhsAccuracy]
                                  (auto&& leafHeader, const long int* particleIndexes,
                                  const std::array<RealType*, 4> particleDataPtr,
                                  const std::array<RealType*, NbRhsValuesPerParticle> particleRhsPtr){
                for(int idxPart = 0 ; idxPart < leafHeader.nbParticles ; ++idxPart){
                    for(int idxValue = 0 ; idxValue < 4 ; ++idxValue){
                       partcilesAccuracy[idxValue] = std::max(TbfUtils::RelativeAccuracy(particleDataPtr[idxValue][idxPart],
                                                                                       particles[idxValue][particleIndexes[idxPart]]),
                                                            partcilesAccuracy[idxValue]);
                    }
                    for(int idxValue = 0 ; idxValue < NbRhsValuesPerParticle ; ++idxValue){
                       partcilesRhsAccuracy[idxValue] = std::max(TbfUtils::RelativeAccuracy(particleRhsPtr[idxValue][idxPart],
                                                                                       particlesRhs[idxValue][particleIndexes[idxPart]]),
                                                            partcilesRhsAccuracy[idxValue]);
                    }
                }
            });

            std::cout << "Relative differences:" << std::endl;
            for(int idxValue = 0 ; idxValue < 4 ; ++idxValue){
               std::cout << " - Data " << idxValue << " = " << partcilesAccuracy[idxValue] << std::endl;
               UASSERTETRUE(partcilesAccuracy[idxValue] < 1e-16);
            }
            for(int idxValue = 0 ; idxValue < 4 ; ++idxValue){
               std::cout << " - Rhs " << idxValue << " = " << partcilesRhsAccuracy[idxValue] << std::endl;
               if constexpr (std::is_same<float, RealType>::value){
                   UASSERTETRUE(partcilesRhsAccuracy[idxValue] < 9e-2);
               }
               else{
                   UASSERTETRUE(partcilesRhsAccuracy[idxValue] < 9e-3);
               }
            }

            //////////////////////////////////////////////////////////////////////

            for(auto& vec : particles){
                delete[] vec;
            }
            for(auto& vec : particlesRhs){
                delete[] vec;
            }
        }

    }

    void TestBasic() {
        for(long int idxNbParticles = 1 ; idxNbParticles <= 10000 ; idxNbParticles *= 10){
            for(const long int idxNbElementsPerBlock : std::vector<long int>{{1, 100, 10000000}}){
                for(const bool idxOneGroupPerParent : std::vector<bool>{{true, false}}){
                    for(long int idxTreeHeight = 1 ; idxTreeHeight < 5 ; ++idxTreeHeight){
                        CorePart(idxNbParticles, idxNbElementsPerBlock, idxOneGroupPerParent, idxTreeHeight);
                    }
                }
            }
        }
    }

    void SetTests() {
        Parent::AddTest(&TestUnifKernel<RealType, AlgorithmClass>::TestBasic, "Basic test based on the test kernel");
    }
};

#endif