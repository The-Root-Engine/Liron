// Root Engine / Liron

#pragma once

#include "LironTatemae.h"
#include "Liron.h"

#include "../Erolyssa/Resources/ErolyssaBarrier.h"

#include <functional>

struct FLironResource
{
    enum class EType { Buffer, Texture } Type = EType::Texture;
    FErolyssaBarrierResourceState State = FErolyssaBarrierResourceState::None;
    
    VkImage RealImage = VK_NULL_HANDLE;
    VkImageView RealView = VK_NULL_HANDLE;
    VkExtent2D RealExtent = { 0, 0 };
    VkBuffer RealBuffer = VK_NULL_HANDLE;
};

struct FLironResourceHandle
{
    uint32 ID = 0xFFFFFFFF;
    bool IsValid() const { return ID != 0xFFFFFFFF; }
    
    static const FLironResourceHandle Invalid;
};

inline constexpr FLironResourceHandle FLironResourceHandle::Invalid  = FLironResourceHandle{ 0xFFFFFFFF };

struct FLironResourceRequirement
{
    FLironResourceHandle Handle;
    FErolyssaBarrierResourceState NeededState;
    
    constexpr FLironResourceRequirement() = default;
    explicit constexpr FLironResourceRequirement(const FLironResourceHandle InHandle, const FErolyssaBarrierResourceState InNeededState)
        : Handle(InHandle), NeededState(InNeededState) {}
};

using FLironPassRequirements = TArray<FLironResourceRequirement, TInlineAllocator<4>>;

struct FLironPass
{
    int32 Priority = 0;
    enum class EType { Compute, Graphics } Type = EType::Compute;
    
    FLironPassRequirements Requirements;
    FLironResourceHandle ColorAttachmentHandle;
    FLironResourceHandle DepthAttachmentHandle;
    
    std::function<void(const FErolyssaCommandBuffer& CommandBuffer)> ExecuteLambda;
};
