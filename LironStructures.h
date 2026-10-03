// Root Engine / Liron

#pragma once

#include "LironTatemae.h"
#include "Liron.h"

#include "../Erolyssa/Resources/ErolyssaBarrier.h"

#include <functional>

struct FLironTextureHandle { uint32 ID = 0xFFFFFFFF; constexpr FLironTextureHandle() = default; explicit constexpr FLironTextureHandle(const uint32 InID) : ID(InID) {} };
struct FLironBufferHandle  { uint32 ID = 0xFFFFFFFF; constexpr FLironBufferHandle () = default; explicit constexpr FLironBufferHandle (const uint32 InID) : ID(InID) {} };

struct FLironPassAccess 
{
    TFixedArray<FLironTextureHandle, 4> ReadTextures ;
    TFixedArray<FLironTextureHandle, 4> WriteTextures;
    
    TFixedArray<FLironBufferHandle, 4> ReadBuffers ;
    TFixedArray<FLironBufferHandle, 4> WriteBuffers;
};

struct FLironPassResourceState 
{
    FErolyssaBarrierResourceState State = FErolyssaBarrierResourceState::None;
    bool bIsPersistent = false;
};

struct FLironPassResourceTextureState 
{
    FErolyssaBarrierResourceState State = FErolyssaBarrierResourceState::None;
    bool bIsPersistent = false;
    
    VkImage     RealImage = VK_NULL_HANDLE;
    VkImageView RealView  = VK_NULL_HANDLE;
    VkExtent2D  Extent    = { 0, 0 };
};

struct FLironPassResourceBufferState 
{
    FErolyssaBarrierResourceState State = FErolyssaBarrierResourceState::None;
    bool bIsPersistent = false;
    
    VkBuffer RealBuffer = VK_NULL_HANDLE;
};

struct FLironPass
{
#if LIRON_ENABLE_DEBUG
    std::string Name;
#endif
    
    FLironPassAccess Access;
    int32 Priority = 0;
    
    std::function<void(const FErolyssaCommandBuffer& CommandBuffer)> ExecuteLambda;
};
