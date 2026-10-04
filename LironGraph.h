// Root Engine / Liron

#pragma once

#include "Liron.h"
#include "LironTatemae.h"
#include "LironStructures.h"

class FLironGraph 
{
    
public:
    FLironGraph()
    {
        Clear();
    }
    
    /*
    FLironTextureHandle RegisterTexture(const std::string_view InName)
    {
        const uint32 NewID = TextureTracker.Num();
        
#if LIRON_ENABLE_DEBUG
        TextureNames.Emplace(InName);
#endif
        
        TextureTracker.Emplace();
        return FLironTextureHandle(NewID);
    }
    
    FLironBufferHandle RegisterBuffer(const std::string_view InName)
    {
        const uint32 NewID = BufferTracker.Num();
        
#if LIRON_ENABLE_DEBUG
        BufferNames.Emplace(InName);
#endif
        
        BufferTracker.Emplace();
        return FLironBufferHandle(NewID);
    }
    */
    
    FLironResourceHandle ImportBuffer(const FErolyssaBuffer& InBuffer, const FErolyssaBarrierResourceState InInitialState = FErolyssaBarrierResourceState::None)
    {
        FLironResource Resource{};
        Resource.Flags = ELironResourceFlags::Buffer;
        Resource.RealBuffer = InBuffer;
        Resource.State = InInitialState;
        
        const FLironResourceHandle Handler { Resources.Num() };
        Resources.Add(Resource);
        return Handler;
    }
    
    FLironResourceHandle ImportTexture(const VkImage InImage, const VkImageView InView, const VkExtent2D InExtent, const bool bIsDepth, const FErolyssaBarrierResourceState InInitialState = FErolyssaBarrierResourceState::None)
    {
        FLironResource Resource{};
        Resource.Flags = ELironResourceFlags::Texture | (bIsDepth ? ELironResourceFlags::Depth : ELironResourceFlags::None);
        Resource.RealImage = InImage;
        Resource.RealView = InView;
        Resource.RealExtent = InExtent;
        Resource.State = InInitialState;
        
        const FLironResourceHandle Handler { Resources.Num() };
        Resources.Add(Resource);
        return Handler;
    }
    
    FLironResourceHandle ImportTexture(const FErolyssaImage& InImage, const bool bIsDepth, const FErolyssaBarrierResourceState InInitialState = FErolyssaBarrierResourceState::None)
    {
        return ImportTexture(InImage, InImage, InImage.GetExtent(), bIsDepth, InInitialState);
    }
    
    void AddComputePass(
        const std::string_view InName, const FLironPassRequirements& InRequirements, const int32 InPriority,
        const std::function<void(const FErolyssaCommandBuffer& CommandBuffer)>& InExecuteLambda
    )
    {
        FLironPass Pass;
        
        Pass.Priority = InPriority;
        Pass.Type = FLironPass::EType::Compute;
        
        Pass.Requirements = InRequirements;
        Pass.ColorAttachmentHandle = FLironResourceHandle::Invalid;
        Pass.DepthAttachmentHandle = FLironResourceHandle::Invalid;
        
        Pass.ExecuteLambda = InExecuteLambda;
        
        Passes.Add(Pass);
    }
    
    void AddGraphicsPass(
        const std::string_view InName, const FLironPassRequirements& InRequirements, const int32 InPriority,
        const FLironResourceHandle InColorAttachmentHandle, const FLironResourceHandle InDepthAttachmentHandle,
        const std::function<void(const FErolyssaCommandBuffer& CommandBuffer)>& InExecuteLambda
    )
    {
        FLironPass Pass;
        
        Pass.Priority = InPriority;
        Pass.Type = FLironPass::EType::Graphics;
        
        Pass.Requirements = InRequirements;
        Pass.ColorAttachmentHandle = InColorAttachmentHandle;
        Pass.DepthAttachmentHandle = InDepthAttachmentHandle;
        
        Pass.ExecuteLambda = InExecuteLambda;
        
        Passes.Add(Pass);
    }
    
    void Execute(const FErolyssaCommandBuffer& InCommandBuffer)
    {
        std::sort(Passes.begin(), Passes.end(), [](const FLironPass& A, const FLironPass& B) { return A.Priority < B.Priority; });
        
        for(const FLironPass& Pass : Passes)
        {
            TArray<FErolyssaBarrierBufferInfo> BufferBarrierInfos;
            TArray<FErolyssaBarrierTextureInfo> TextureBarrierInfos;
            
            const bool bClearColor = Pass.Type == FLironPass::EType::Graphics && Pass.ColorAttachmentHandle.IsValid() && Resources[Pass.ColorAttachmentHandle.ID].State == FErolyssaBarrierResourceState::None;
            const bool bClearDepth = Pass.Type == FLironPass::EType::Graphics && Pass.DepthAttachmentHandle.IsValid() && Resources[Pass.DepthAttachmentHandle.ID].State == FErolyssaBarrierResourceState::None;
            
            for(const FLironResourceRequirement& Requirement : Pass.Requirements)
            {
                FLironResource& Resource = Resources[Requirement.Handle.ID];
                if(Resource.State == Requirement.NeededState) continue;
                
                /**/ if(EnumHasAnyFlags(Resource.Flags, ELironResourceFlags::Buffer))
                {
                    FErolyssaBarrierBufferInfo BufferBarrierInfo;
                    BufferBarrierInfo.Buffer = Resource.RealBuffer;
                    BufferBarrierInfo.Offset = 0;
                    BufferBarrierInfo.Size = VK_WHOLE_SIZE;
                    BufferBarrierInfo.OldState = Resource.State;
                    BufferBarrierInfo.NewState = Requirement.NeededState;
                    BufferBarrierInfos.Add(BufferBarrierInfo);
                }
                else if(EnumHasAnyFlags(Resource.Flags, ELironResourceFlags::Texture))
                {
                    FErolyssaBarrierTextureInfo TextureBarrierInfo;
                    TextureBarrierInfo.Image = Resource.RealImage;
                    TextureBarrierInfo.AspectMask = EnumHasAnyFlags(Resource.Flags, ELironResourceFlags::Depth) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                    TextureBarrierInfo.OldState = Resource.State;
                    TextureBarrierInfo.NewState = Requirement.NeededState;
                    TextureBarrierInfos.Add(TextureBarrierInfo);
                }
                
                Resource.State = Requirement.NeededState;
            }
            
            if(!BufferBarrierInfos.IsEmpty() || !TextureBarrierInfos.IsEmpty())
                FErolyssaBarrier::Insert(InCommandBuffer, BufferBarrierInfos, TextureBarrierInfos);
            
            if(Pass.Type == FLironPass::EType::Graphics)
            {
                VkRenderingInfo RenderingInfo{};
                RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                RenderingInfo.layerCount = 1;
                
                VkRenderingAttachmentInfo ColorAttachment{};
                VkRenderingAttachmentInfo DepthAttachment{};
                
                if(Pass.ColorAttachmentHandle.IsValid())
                {
                    const FLironResource& TextureResource = Resources[Pass.ColorAttachmentHandle.ID];
                    
                    ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    ColorAttachment.imageView = TextureResource.RealView;
                    ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    ColorAttachment.loadOp = bClearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                    ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    ColorAttachment.clearValue.color.uint32[0] = 0xFFFFFFFF;
                    ColorAttachment.clearValue.color.uint32[1] = 0xFFFFFFFF;
                    ColorAttachment.clearValue.color.uint32[2] = 0xFFFFFFFF;
                    ColorAttachment.clearValue.color.uint32[3] = 0xFFFFFFFF;
                    
                    RenderingInfo.colorAttachmentCount = 1;
                    RenderingInfo.pColorAttachments = &ColorAttachment;
                    RenderingInfo.renderArea.extent = TextureResource.RealExtent;
                }
                
                if(Pass.DepthAttachmentHandle.IsValid())
                {
                    const FLironResource& DepthResource = Resources[Pass.DepthAttachmentHandle.ID];
                    
                    DepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    DepthAttachment.imageView = DepthResource.RealView;
                    DepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    DepthAttachment.loadOp = bClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                    DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    DepthAttachment.clearValue.depthStencil.depth   = 0.f;
                    DepthAttachment.clearValue.depthStencil.stencil = 0.f;
                    
                    RenderingInfo.pDepthAttachment = &DepthAttachment;
                    
                    if(RenderingInfo.renderArea.extent.width == 0)
                        RenderingInfo.renderArea.extent = DepthResource.RealExtent;
                }
                
                vkCmdBeginRendering(InCommandBuffer, &RenderingInfo);
            }
            
            Pass.ExecuteLambda(InCommandBuffer);
            
            if(Pass.Type == FLironPass::EType::Graphics)
            {
                vkCmdEndRendering(InCommandBuffer);
            }
        }
    }
    
    void PrepareForPresent(const FErolyssaCommandBuffer& CmdBuffer, const FLironResourceHandle SwapchainHandle)
    {
        FLironResource& SwapchainResource = Resources[SwapchainHandle.ID];
        if(SwapchainResource.State == FErolyssaBarrierResourceState::Present) return;
        
        TArray<FErolyssaBarrierTextureInfo> PresentBarriers;
        
        FErolyssaBarrierTextureInfo TextureBarrierInfo;
        TextureBarrierInfo.Image      = SwapchainResource.RealImage;
        TextureBarrierInfo.OldState   = SwapchainResource.State;
        TextureBarrierInfo.NewState   = FErolyssaBarrierResourceState::Present;
        TextureBarrierInfo.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        PresentBarriers.Add(TextureBarrierInfo);
        
        FErolyssaBarrier::Insert(CmdBuffer, {}, PresentBarriers);
        SwapchainResource.State = FErolyssaBarrierResourceState::Present;
    }
    
    void Clear()
    {
        Passes.Empty();
        Resources.Empty();
    }

private:
    TArray<FLironPass> Passes;
    TArray<FLironResource> Resources;
    
    /*
    enum class EPassColor : uint8 { White, Gray, Black };
    
    bool PassWritesToTexture(const uint32 PassIndex, const uint32 TextureID) const
    {
        for(const FLironTextureHandle& TextureHandle : Passes[PassIndex].Access.WriteTextures)
            if(TextureHandle.ID == TextureID) return true;
        
        return false;
    }
    
    bool PassWritesToBuffer(const uint32 PassIndex, const uint32 BufferID) const
    {
        for(const FLironBufferHandle& BufferHandle : Passes[PassIndex].Access.WriteBuffers)
            if(BufferHandle.ID == BufferID) return true;
        
        return false;
    }
    
    void BuildExecutionOrder(TArray<uint32>& OutOrder)
    {
        std::vector<EPassColor> PassColors(Passes.Num(), EPassColor::White);
        OutOrder.Empty();
        
        std::function<void(uint32)> VisitPass = [&](uint32 PassIndex) {
            if (PassColors[PassIndex] == EPassColor::Black) return;
            check(PassColors[PassIndex] != EPassColor::Gray, "Cyclic dependency detected!");

            PassColors[PassIndex] = EPassColor::Gray;
            const auto& CurrentPass = Passes[PassIndex];
            
            // ---------------------------------------------------------------------
            // 1. СВЯЗЬ ПО ЧТЕНИЮ (RaW): Ищем, кто писал в то, что мы читаем
            // ---------------------------------------------------------------------
            
            // Проверка зависимостей по ТЕКСТУРАМ
            for (size_t i = 0; i < CurrentPass.Access.ReadTextures.Num(); ++i) {
                uint32 ReadTexID = CurrentPass.Access.ReadTextures[i].ID;
                for (uint32 PrevIdx = 0; PrevIdx < Passes.Num(); ++PrevIdx) {
                    if (PrevIdx == PassIndex) continue;
                    if (PassWritesToTexture(PrevIdx, ReadTexID)) {
                        VisitPass(PrevIdx);
                    }
                }
            }

            // Проверка зависимостей по БУФЕРАМ
            for (size_t i = 0; i < CurrentPass.Access.ReadBuffers.Num(); ++i) {
                uint32 ReadBufID = CurrentPass.Access.ReadBuffers[i].ID;
                for (uint32 PrevIdx = 0; PrevIdx < Passes.Num(); ++PrevIdx) {
                    if (PrevIdx == PassIndex) continue;
                    if (PassWritesToBuffer(PrevIdx, ReadBufID)) {
                        VisitPass(PrevIdx);
                    }
                }
            }
            
            // ---------------------------------------------------------------------
            // 2. СВЯЗЬ ПО ЗАПИСИ (WaW): Разруливаем приоритеты при записи в один ресурс
            // ---------------------------------------------------------------------
            
            // Разруливаем WaW конфликты для ТЕКСТУР
            for (size_t i = 0; i < CurrentPass.Access.WriteTextures.Num(); ++i) {
                uint32 WriteTexID = CurrentPass.Access.WriteTextures[i].ID;
                for (uint32 PrevIdx = 0; PrevIdx < Passes.Num(); ++PrevIdx) {
                    if (PrevIdx == PassIndex) continue;
                    
                    if (PassWritesToTexture(PrevIdx, WriteTexID)) {
                        if (Passes[PrevIdx].Priority < CurrentPass.Priority) {
                            VisitPass(PrevIdx);
                        }
                    }
                }
            }

            // Разруливаем WaW конфликты для БУФЕРАВ (например, два вычислительных пасса пишут в один буфер аргументов)
            for (size_t i = 0; i < CurrentPass.Access.WriteBuffers.Num(); ++i) {
                uint32 WriteBufID = CurrentPass.Access.WriteBuffers[i].ID;
                for (uint32 PrevIdx = 0; PrevIdx < Passes.Num(); ++PrevIdx) {
                    if (PrevIdx == PassIndex) continue;
                    
                    if (PassWritesToBuffer(PrevIdx, WriteBufID)) {
                        if (Passes[PrevIdx].Priority < CurrentPass.Priority) {
                            VisitPass(PrevIdx);
                        }
                    }
                }
            }
            
            PassColors[PassIndex] = EPassColor::Black;
            OutOrder.Add(PassIndex);
        };
        
        std::vector<uint32> SortedIndices(Passes.Num());
        for (uint32 i = 0; i < Passes.Num(); ++i) SortedIndices[i] = i;
        
        std::sort(SortedIndices.begin(), SortedIndices.end(), [this](uint32 a, uint32 b) {
            return Passes[a].Priority > Passes[b].Priority;
        });
        
        for (uint32 i : SortedIndices) {
            if (PassColors[i] == EPassColor::White) VisitPass(i);
        }
    }
    */
};
