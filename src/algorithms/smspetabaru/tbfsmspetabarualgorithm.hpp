#ifndef TBFSMSPETABARUALGORITHM_HPP
#define TBFSMSPETABARUALGORITHM_HPP

#include "tbfglobal.hpp"

#include "../sequential/tbfgroupkernelinterface.hpp"
#include "spacial/tbfspacialconfiguration.hpp"
#include "algorithms/tbfalgorithmutils.hpp"

#include <Runtimes/SpRuntime.hpp>


#include <cassert>
#include <iterator>

template <class RealType_T, class KernelClass_T, class SpaceIndexType_T = TbfDefaultSpaceIndexType<RealType_T>>
class TbfSmSpetabaruAlgorithm {
public:
    using RealType = RealType_T;
    using KernelClass = KernelClass_T;
    using SpaceIndexType = SpaceIndexType_T;
    using SpacialConfiguration = TbfSpacialConfiguration<RealType, SpaceIndexType::Dim>;

protected:
    const SpacialConfiguration configuration;
    const SpaceIndexType spaceSystem;

    TbfGroupKernelInterface<SpaceIndexType> kernelWrapper;
    KernelClass kernel;

    TbfAlgorithmUtils::LFmmOperationsPriorities priorities;

    template <class TreeClass>
    void P2M(SpRuntime& runtime, TreeClass& inTree){
        if(configuration.getTreeHeight() > 2){
            auto& leafGroups = inTree.getLeafGroups();
            const auto& particleGroups = inTree.getParticleGroups();

            assert(std::size(leafGroups) == std::size(particleGroups));

            auto currentLeafGroup = leafGroups.begin();
            auto currentParticleGroup = particleGroups.cbegin();

            const auto endLeafGroup = leafGroups.end();
            const auto endParticleGroup = particleGroups.cend();

            while(currentLeafGroup != endLeafGroup && currentParticleGroup != endParticleGroup){
                assert((*currentParticleGroup).getStartingSpacialIndex() == (*currentLeafGroup).getStartingSpacialIndex()
                       && (*currentParticleGroup).getEndingSpacialIndex() == (*currentLeafGroup).getEndingSpacialIndex()
                       && (*currentParticleGroup).getNbLeaves() == (*currentLeafGroup).getNbCells());
                auto& leafGroupObj = *currentLeafGroup;
                const auto& particleGroupObj = *currentParticleGroup;
                runtime.task(SpPriority(priorities.getP2MPriority()), SpRead(*particleGroupObj.getDataPtr()), SpCommuteWrite(*leafGroupObj.getMultipolePtr()),
                                   [this, &leafGroupObj, &particleGroupObj](const unsigned char&, unsigned char&){
                    kernelWrapper.P2M(kernel, particleGroupObj, leafGroupObj);
                });
                ++currentParticleGroup;
                ++currentLeafGroup;
            }
        }
    }

    template <class TreeClass>
    void M2M(SpRuntime& runtime, TreeClass& inTree){
        for(long int idxLevel = configuration.getTreeHeight()-2 ; idxLevel >= 2 ; --idxLevel){// TODO parent
            auto& upperCellGroup = inTree.getCellGroupsAtLevel(idxLevel);
            const auto& lowerCellGroup = inTree.getCellGroupsAtLevel(idxLevel+1);

            auto currentUpperGroup = upperCellGroup.begin();
            auto currentLowerGroup = lowerCellGroup.cbegin();

            const auto endUpperGroup = upperCellGroup.end();
            const auto endLowerGroup = lowerCellGroup.cend();

            while(currentUpperGroup != endUpperGroup && currentLowerGroup != endLowerGroup){
                assert(spaceSystem.getParentIndex(currentLowerGroup->getStartingSpacialIndex()) <= currentUpperGroup->getEndingSpacialIndex()
                       || currentUpperGroup->getStartingSpacialIndex() <= spaceSystem.getParentIndex(currentLowerGroup->getEndingSpacialIndex()));

                auto& upperGroup = *currentUpperGroup;
                const auto& lowerGroup = *currentLowerGroup;
                runtime.task(SpPriority(priorities.getM2MPriority(idxLevel)), SpRead(*lowerGroup.getMultipolePtr()), SpCommuteWrite(*upperGroup.getMultipolePtr()),
                                   [this, idxLevel, &upperGroup, &lowerGroup](const unsigned char&, unsigned char&){
                    kernelWrapper.M2M(idxLevel, kernel, lowerGroup, upperGroup);
                });

                if(spaceSystem.getParentIndex(currentLowerGroup->getEndingSpacialIndex()) <= currentUpperGroup->getEndingSpacialIndex()){
                    ++currentLowerGroup;
                    if(currentLowerGroup != endLowerGroup && currentUpperGroup->getEndingSpacialIndex() < spaceSystem.getParentIndex(currentLowerGroup->getStartingSpacialIndex())){
                        ++currentUpperGroup;
                    }
                }
                else{
                    ++currentUpperGroup;
                }
            }
        }
    }

    template <class TreeClass>
    void M2L(SpRuntime& runtime, TreeClass& inTree){
        const auto& spacialSystem = inTree.getSpacialSystem();

        for(long int idxLevel = 2 ; idxLevel <= configuration.getTreeHeight()-1 ; ++idxLevel){
            auto& cellGroups = inTree.getCellGroupsAtLevel(idxLevel);

            auto currentCellGroup = cellGroups.begin();
            const auto endCellGroup = cellGroups.end();

            while(currentCellGroup != endCellGroup){
                auto indexesForGroup = spacialSystem.getInteractionListForBlock(*currentCellGroup, idxLevel);
                TbfAlgorithmUtils::TbfMapIndexesAndBlocks(std::move(indexesForGroup.second), cellGroups, std::distance(cellGroups.begin(),currentCellGroup),
                                               [&](auto& groupTarget, const auto& groupSrc, const auto& indexes){
                    assert(&groupTarget == &*currentCellGroup);

                    runtime.task(SpPriority(priorities.getM2LPriority(idxLevel)), SpRead(*groupSrc.getMultipolePtr()), SpCommuteWrite(*groupTarget.getLocalPtr()),
                                       [this, idxLevel, indexesVec = indexes.toStdVector(), &groupSrc, &groupTarget](const unsigned char&, unsigned char&){
                        kernelWrapper.M2LBetweenGroups(idxLevel, kernel, groupTarget, groupSrc, std::move(indexesVec));
                    });
                });

                auto& currentGroup = *currentCellGroup;
                runtime.task(SpPriority(priorities.getM2LPriority(idxLevel)), SpRead(*currentGroup.getMultipolePtr()), SpCommuteWrite(*currentGroup.getLocalPtr()),
                                   [this, idxLevel, indexesForGroup_first = std::move(indexesForGroup.first), &currentGroup](const unsigned char&, unsigned char&){
                    kernelWrapper.M2LInGroup(idxLevel, kernel, currentGroup, indexesForGroup_first);
                });

                ++currentCellGroup;
            }
        }
    }

    template <class TreeClass>
    void L2L(SpRuntime& runtime, TreeClass& inTree){
        for(long int idxLevel = 2 ; idxLevel <= configuration.getTreeHeight()-2 ; ++idxLevel){
            const auto& upperCellGroup = inTree.getCellGroupsAtLevel(idxLevel);
            auto& lowerCellGroup = inTree.getCellGroupsAtLevel(idxLevel+1);

            auto currentUpperGroup = upperCellGroup.cbegin();
            auto currentLowerGroup = lowerCellGroup.begin();

            const auto endUpperGroup = upperCellGroup.cend();
            const auto endLowerGroup = lowerCellGroup.end();

            while(currentUpperGroup != endUpperGroup && currentLowerGroup != endLowerGroup){
                assert(spaceSystem.getParentIndex(currentLowerGroup->getStartingSpacialIndex()) <= currentUpperGroup->getEndingSpacialIndex()
                       || currentUpperGroup->getStartingSpacialIndex() <= spaceSystem.getParentIndex(currentLowerGroup->getEndingSpacialIndex()));

                const auto& upperGroup = *currentUpperGroup;
                auto& lowerGroup = *currentLowerGroup;
                runtime.task(SpPriority(priorities.getL2LPriority(idxLevel)), SpRead(*upperGroup.getLocalPtr()), SpCommuteWrite(*lowerGroup.getLocalPtr()),
                                   [this, idxLevel, &upperGroup, &lowerGroup](const unsigned char&, unsigned char&){
                    kernelWrapper.L2L(idxLevel, kernel, upperGroup, lowerGroup);
                });

                if(spaceSystem.getParentIndex(currentLowerGroup->getEndingSpacialIndex()) <= currentUpperGroup->getEndingSpacialIndex()){
                    ++currentLowerGroup;
                    if(currentLowerGroup != endLowerGroup && currentUpperGroup->getEndingSpacialIndex() < spaceSystem.getParentIndex(currentLowerGroup->getStartingSpacialIndex())){
                        ++currentUpperGroup;
                    }
                }
                else{
                    ++currentUpperGroup;
                }
            }
        }
    }

    template <class TreeClass>
    void L2P(SpRuntime& runtime, TreeClass& inTree){
        if(configuration.getTreeHeight() > 2){
            const auto& leafGroups = inTree.getLeafGroups();
            auto& particleGroups = inTree.getParticleGroups();

            assert(std::size(leafGroups) == std::size(particleGroups));

            auto currentLeafGroup = leafGroups.cbegin();
            auto currentParticleGroup = particleGroups.begin();

            const auto endLeafGroup = leafGroups.cend();
            const auto endParticleGroup = particleGroups.end();

            while(currentLeafGroup != endLeafGroup && currentParticleGroup != endParticleGroup){
                assert((*currentParticleGroup).getStartingSpacialIndex() == (*currentLeafGroup).getStartingSpacialIndex()
                       && (*currentParticleGroup).getEndingSpacialIndex() == (*currentLeafGroup).getEndingSpacialIndex()
                       && (*currentParticleGroup).getNbLeaves() == (*currentLeafGroup).getNbCells());

                const auto& leafGroupObj = *currentLeafGroup;
                auto& particleGroupObj = *currentParticleGroup;
                runtime.task(SpPriority(priorities.getL2PPriority()), SpRead(*leafGroupObj.getLocalPtr()), SpCommuteWrite(*particleGroupObj.getRhsPtr()),
                                   [this, &leafGroupObj, &particleGroupObj](const unsigned char&, unsigned char&){
                    kernelWrapper.L2P(kernel, leafGroupObj, particleGroupObj);
                });

                ++currentParticleGroup;
                ++currentLeafGroup;
            }
        }
    }

    template <class TreeClass>
    void P2P(SpRuntime& runtime, TreeClass& inTree){
        const auto& spacialSystem = inTree.getSpacialSystem();

        auto& particleGroups = inTree.getParticleGroups();

        auto currentParticleGroup = particleGroups.begin();
        const auto endParticleGroup = particleGroups.end();

        while(currentParticleGroup != endParticleGroup){

            auto indexesForGroup = spacialSystem.getNeighborListForBlock(*currentParticleGroup, configuration.getTreeHeight()-1, true);
            TbfAlgorithmUtils::TbfMapIndexesAndBlocks(std::move(indexesForGroup.second), particleGroups, std::distance(particleGroups.begin(), currentParticleGroup),
                                           [&](auto& groupTarget, auto& groupSrc, const auto& indexes){
                assert(&groupTarget == &*currentParticleGroup);

                runtime.task(SpPriority(priorities.getP2PPriority()), SpCommuteWrite(*groupSrc.getDataPtr()), SpCommuteWrite(*groupTarget.getRhsPtr()),
                                   [this, indexesVec = indexes.toStdVector(), &groupSrc, &groupTarget](const unsigned char&, unsigned char&){
                    kernelWrapper.P2PBetweenGroups(kernel, groupTarget, groupSrc, std::move(indexesVec));
                });

            });

            auto& currentGroup = *currentParticleGroup;
            runtime.task(SpPriority(priorities.getP2PPriority()), SpRead(*currentGroup.getDataPtr()),SpCommuteWrite(*currentGroup.getRhsPtr()),
                               [this, indexesForGroup_first = std::move(indexesForGroup.first), &currentGroup](const unsigned char&, unsigned char&){
                kernelWrapper.P2PInGroup(kernel, currentGroup, indexesForGroup_first);

                kernelWrapper.P2PInner(kernel, currentGroup);
            });

            ++currentParticleGroup;
        }
    }

public:
    TbfSmSpetabaruAlgorithm(const SpacialConfiguration& inConfiguration)
        : configuration(inConfiguration), spaceSystem(configuration),
          kernelWrapper(configuration), kernel(configuration),
          priorities(configuration.getTreeHeight()){
    }

    template <class SourceKernelClass>
    TbfSmSpetabaruAlgorithm(const SpacialConfiguration& inConfiguration, SourceKernelClass&& inKernel)
        : configuration(inConfiguration), spaceSystem(configuration),
          kernelWrapper(configuration), kernel(std::forward<SourceKernelClass>(inKernel)),
          priorities(configuration.getTreeHeight()){
    }

    template <class TreeClass>
    void execute(TreeClass& inTree, const int inOperationToProceed = TbfAlgorithmUtils::LFmmOperations::LFmmNearAndFarFields){
        assert(configuration == inTree.getSpacialConfiguration());

        SpRuntime runtime;

        if(inOperationToProceed & TbfAlgorithmUtils::LFmmP2M){
            P2M(runtime, inTree);
        }
        if(inOperationToProceed & TbfAlgorithmUtils::LFmmM2M){
            M2M(runtime, inTree);
        }
        if(inOperationToProceed & TbfAlgorithmUtils::LFmmM2L){
            M2L(runtime, inTree);
        }
        if(inOperationToProceed & TbfAlgorithmUtils::LFmmL2L){
            L2L(runtime, inTree);
        }
        if(inOperationToProceed & TbfAlgorithmUtils::LFmmL2P){
            L2P(runtime, inTree);
        }
        if(inOperationToProceed & TbfAlgorithmUtils::LFmmP2P){
            P2P(runtime, inTree);
        }

        runtime.waitAllTasks();
    }
};

#endif