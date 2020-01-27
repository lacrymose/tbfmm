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
#include "kernels/testkernel/tbftestkernel.hpp"
#include "algorithms/tbfalgorithmutils.hpp"


template <class AlgorithmClass>
class TestTestKernel : public UTester< TestTestKernel<AlgorithmClass> > {
    using Parent = UTester< TestTestKernel<AlgorithmClass> >;
    using RealType = typename AlgorithmClass::RealType;

    void CorePart(const long int NbParticles, const long int inNbElementsPerBlock,
                  const bool inOneGroupPerParent, const long int TreeHeight){
        const int Dim = 3;

        /////////////////////////////////////////////////////////////////////////////////////////

        const std::array<RealType, Dim> BoxWidths{{1, 1, 1}};
        const std::array<RealType, Dim> inBoxCenter{{0.5, 0.5, 0.5}};

        const TbfSpacialConfiguration<RealType, Dim> configuration(TreeHeight, BoxWidths, inBoxCenter);

        /////////////////////////////////////////////////////////////////////////////////////////

        TbfRandom<RealType, Dim> randomGenerator(configuration.getBoxWidths());

        std::vector<std::array<RealType, Dim>> particlePositions(NbParticles);

        for(long int idxPart = 0 ; idxPart < NbParticles ; ++idxPart){
            particlePositions[idxPart] = randomGenerator.getNewItem();
        }

        /////////////////////////////////////////////////////////////////////////////////////////

        constexpr long int NbDataValuesPerParticle = Dim;
        constexpr long int NbRhsValuesPerParticle = 1;
        using MultipoleClass = std::array<long int,1>;
        using LocalClass = std::array<long int,1>;

        TbfDefaultSpaceIndexType<RealType> spacialSystem(configuration);

        {
            TbfParticlesContainer<RealType, RealType, NbDataValuesPerParticle, long int, NbRhsValuesPerParticle> particles(spacialSystem, particlePositions);

            std::vector<typename TbfDefaultSpaceIndexType<RealType>::IndexType> leafIndexes(particles.getNbLeaves());

            for(long int idxLeaf = 0 ; idxLeaf < particles.getNbLeaves() ; ++idxLeaf){
                leafIndexes[idxLeaf] = particles.getLeafSpacialIndex(idxLeaf);
            }

            TbfCellsContainer<RealType, MultipoleClass, LocalClass> cells(leafIndexes);
        }

        /////////////////////////////////////////////////////////////////////////////////////////

        if(TreeHeight > 2){
            TbfTree<RealType, RealType, NbDataValuesPerParticle, long int, NbRhsValuesPerParticle, MultipoleClass, LocalClass> tree(configuration, inNbElementsPerBlock,
                                                                                        particlePositions, inOneGroupPerParent);

            AlgorithmClass algorithm(configuration);
            algorithm.execute(tree, TbfAlgorithmUtils::LFmmP2M | TbfAlgorithmUtils::LFmmM2M | TbfAlgorithmUtils::LFmmM2L);

            tree.applyToAllLeaves([this, &tree, NbParticles, TreeHeight](auto&& leafHeader, const long int* /*particleIndexes*/,
                                  const std::array<RealType*, Dim> /*particleDataPtr*/, const std::array<long int*, 1> /*particleRhsPtr*/){
                auto groupForCell = tree.findGroupWithCell(TreeHeight-1, leafHeader.spaceIndex);

                if(!groupForCell){
                    throw std::runtime_error("Stop here");
                }

                auto multipoleData = (*groupForCell).first.get().getCellMultipole((*groupForCell).second);

                assert(leafHeader.nbParticles);

                UASSERTEEQUAL(multipoleData[0], leafHeader.nbParticles);
            });

            tree.applyToAllCells([this, &spacialSystem,&tree, TreeHeight](const long int inLevel, auto&& cellHeader,
                                 const std::optional<std::reference_wrapper<MultipoleClass>> cellMultipole,
                                 const std::optional<std::reference_wrapper<LocalClass>> /*cellLocalr*/){
                if(1 < inLevel && inLevel < TreeHeight-1){
                    const int NbChild = (1 << Dim);
                    long int totalSum = 0;
                    for(long int idxChild = 0 ; idxChild < NbChild ; ++idxChild){
                        auto indexChild = spacialSystem.getChildIndexFromParent(cellHeader.spaceIndex, idxChild);
                        auto groupForCell = tree.findGroupWithCell(inLevel+1, indexChild);
                        if(groupForCell){
                            auto multipoleData = (*groupForCell).first.get().getCellMultipole((*groupForCell).second);
                            totalSum += multipoleData[0];
                        }
                    }

                    assert(totalSum);

                    UASSERTEEQUAL((*cellMultipole).get()[0], totalSum);
                }
            });

            tree.applyToAllCells([this, &spacialSystem,&tree](const long int inLevel, auto&& cellHeader,
                                 const std::optional<std::reference_wrapper<MultipoleClass>> /*cellMultipole*/,
                                 const std::optional<std::reference_wrapper<LocalClass>> cellLocalr){
                auto indexes = spacialSystem.getInteractionListForIndex(cellHeader.spaceIndex, inLevel);
                long int totalSum = 0;
                for(auto index : indexes){
                    auto groupForCell = tree.findGroupWithCell(inLevel, index);
                    if(groupForCell){
                        auto multipoleData = (*groupForCell).first.get().getCellMultipole((*groupForCell).second);
                        totalSum += multipoleData[0];
                    }
                }
                UASSERTEEQUAL((*cellLocalr).get()[0], totalSum);
            });
        }

        {
            TbfTree<RealType, RealType, NbDataValuesPerParticle, long int, NbRhsValuesPerParticle, MultipoleClass, LocalClass> tree(configuration, inNbElementsPerBlock,
                                                                                        particlePositions, inOneGroupPerParent);

            AlgorithmClass algorithm(configuration);
            algorithm.execute(tree, TbfAlgorithmUtils::LFmmP2P);

            tree.applyToAllLeaves([this, &tree, &spacialSystem, TreeHeight](auto&& leafHeader, const long int* /*particleIndexes*/,
                                  const std::array<RealType*, Dim> /*particleDataPtr*/, const std::array<long int*, 1> particleRhsPtr){
                auto indexes = spacialSystem.getNeighborListForBlock(leafHeader.spaceIndex, TreeHeight-1);
                long int totalSum = 0;
                for(auto index : indexes){
                    auto groupForLeaf = tree.findGroupWithLeaf(index);
                    if(groupForLeaf){
                        auto nbParticles = (*groupForLeaf).first.get().getNbParticlesInLeaf((*groupForLeaf).second);
                        totalSum += nbParticles;
                    }
                }

                totalSum += leafHeader.nbParticles - 1;

                for(int idxPart = 0 ; idxPart < leafHeader.nbParticles ; ++idxPart){
                    UASSERTEEQUAL(particleRhsPtr[0][idxPart], totalSum);
                }
            });
        }

        {
            TbfTree<RealType, RealType, NbDataValuesPerParticle, long int, NbRhsValuesPerParticle, MultipoleClass, LocalClass> tree(configuration, inNbElementsPerBlock,
                                                                                        particlePositions, inOneGroupPerParent);

            AlgorithmClass algorithm(configuration);
            algorithm.execute(tree);

            tree.applyToAllCells([this, &spacialSystem,&tree](const long int inLevel, auto&& cellHeader,
                                 const std::optional<std::reference_wrapper<MultipoleClass>> /*cellMultipole*/,
                                 const std::optional<std::reference_wrapper<LocalClass>> cellLocalr){
                auto indexes = spacialSystem.getInteractionListForIndex(cellHeader.spaceIndex, inLevel);
                long int totalSum = 0;
                for(auto index : indexes){
                    auto groupForCell = tree.findGroupWithCell(inLevel, index);
                    if(groupForCell){
                        auto multipoleData = (*groupForCell).first.get().getCellMultipole((*groupForCell).second);
                        totalSum += multipoleData[0];
                    }
                }
                if(2 < inLevel){
                    auto parentIndex = spacialSystem.getParentIndex(cellHeader.spaceIndex);
                    auto groupForCell = tree.findGroupWithCell(inLevel-1, parentIndex);
                    if(!groupForCell){
                        throw std::runtime_error("Stop here");
                    }

                    auto parentLocalData = (*groupForCell).first.get().getCellLocal((*groupForCell).second);
                    totalSum += parentLocalData[0];
                }
                UASSERTEEQUAL((*cellLocalr).get()[0], totalSum);
            });


            tree.applyToAllLeaves([this, NbParticles](auto&& leafHeader, const long int* /*particleIndexes*/,
                                  const std::array<RealType*, Dim> /*particleDataPtr*/, const std::array<long int*, 1> particleRhsPtr){
                for(int idxPart = 0 ; idxPart < leafHeader.nbParticles ; ++idxPart){
                    UASSERTEEQUAL(particleRhsPtr[0][idxPart], NbParticles-1);
                }
            });
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
        Parent::AddTest(&TestTestKernel<AlgorithmClass>::TestBasic, "Basic test based on the test kernel");
    }
};

#endif