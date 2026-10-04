// Root Engine / Liron

#pragma once

#include "Liron.h"
#include "LironTatemae.h"
#include "LironStructures.h"

#include <utility>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

class FLironGraph 
{
    
public:
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
        Resource.Type = FLironResource::EType::Buffer;
        Resource.RealBuffer = InBuffer;
        Resource.State = InInitialState;
        
        const FLironResourceHandle Handler { Resources.Num() };
        Resources.Add(Resource);
        return Handler;
    }
    
    FLironResourceHandle ImportTexture(const VkImage InImage, const VkImageView InView, const VkExtent2D InExtent, const FErolyssaBarrierResourceState InInitialState = FErolyssaBarrierResourceState::None)
    {
        FLironResource Resource{};
        Resource.Type = FLironResource::EType::Texture;
        Resource.RealImage = InImage;
        Resource.RealView = InView;
        Resource.RealExtent = InExtent;
        Resource.State = InInitialState;
        
        const FLironResourceHandle Handler { Resources.Num() };
        Resources.Add(Resource);
        return Handler;
    }
    
    void AddComputePass(
        const std::string_view InName, FLironPassRequirements InRequirements, const int32 InPriority,
        const std::function<void(const FErolyssaCommandBuffer& CommandBuffer)>& InExecuteLambda
    )
    {
        FLironPass Pass;
        
        Pass.Priority = InPriority;
        Pass.Type = FLironPass::EType::Compute;
        
        Pass.Requirements = Move(InRequirements);
        Pass.ColorAttachmentHandle = FLironResourceHandle::Invalid;
        Pass.DepthAttachmentHandle = FLironResourceHandle::Invalid;
        
        Pass.ExecuteLambda = InExecuteLambda;
        
        Passes.Add(Pass);
    }
    
    void AddGraphicsPass(
        const std::string_view InName, FLironPassRequirements InRequirements, const int32 InPriority,
        const FLironResourceHandle InColorAttachmentHandle, const FLironResourceHandle InDepthAttachmentHandle,
        const std::function<void(const FErolyssaCommandBuffer& CommandBuffer)>& InExecuteLambda
    )
    {
        FLironPass Pass;
        
        Pass.Priority = InPriority;
        Pass.Type = FLironPass::EType::Graphics;
        
        Pass.Requirements = Move(InRequirements);
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
                
                switch (Resource.Type)
                {
                case FLironResource::EType::Buffer:
                    {
                        FErolyssaBarrierBufferInfo BufferBarrierInfo;
                        BufferBarrierInfo.Buffer = Resource.RealBuffer;
                        BufferBarrierInfo.Offset = 0;
                        BufferBarrierInfo.Size = VK_WHOLE_SIZE;
                        BufferBarrierInfo.OldState = Resource.State;
                        BufferBarrierInfo.NewState = Requirement.NeededState;
                        BufferBarrierInfos.Add(BufferBarrierInfo);
                    }
                    break;
                    
                case FLironResource::EType::Texture:
                    {
                        FErolyssaBarrierTextureInfo TextureBarrierInfo;
                        TextureBarrierInfo.Image = Resource.RealImage;
                        TextureBarrierInfo.AspectMask = Requirement.NeededState == FErolyssaBarrierResourceState::DepthStencilAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                        TextureBarrierInfo.OldState = Resource.State;
                        TextureBarrierInfo.NewState = Requirement.NeededState;
                        TextureBarrierInfos.Add(TextureBarrierInfo);
                    }
                    break;
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
                    DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    DepthAttachment.clearValue.depthStencil.depth = 1.0f;
                    DepthAttachment.clearValue.depthStencil.stencil = 0;
                    
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
    
    /*
    void Execute(const FErolyssaCommandBuffer& InCommandBuffer)
    {
        // 1. Получаем правильный порядок выполнения пассов
        TArray<uint32> ExecutionOrder;
        BuildExecutionOrder(ExecutionOrder);
    
        // Временный трекер на кадр, чтобы знать, какие ресурсы мы уже трогали.
        // На его основе мы будем автоматически переключать loadOp (CLEAR -> LOAD).
        // Используем std::vector как временную битовую маску на CPU.
        std::vector<bool> TextureTouched(TextureTracker.Num(), false);
        std::vector<bool> BufferTouched(BufferTracker.Num(), false);

        // 2. Итерируемся по отсортированным пассам кадра
        for (const uint32 PassIndex : ExecutionOrder)
        {
            const FLironPass& Pass = Passes[PassIndex];
            if (!Pass.ExecuteLambda) continue;

            // Временные массивы для сбора барьеров именно для ЭТОГО пасса.
            // В реальном движке ты можешь зарезервировать под них память на MemStack.
            std::vector<VkImageMemoryBarrier2>  ImageBarriers;
            std::vector<VkBufferMemoryBarrier2> BufferBarriers;

            // =====================================================================
            // 1. СИНХРОНИЗАЦИЯ БУФЕРОВ (Read & Write)
            // =====================================================================
        
            // Обрабатываем буферы чтения (ReadBuffers)
            for (size_t i = 0; i < Pass.Access.ReadBuffers.Num(); ++i)
            {
                uint32 BufID = Pass.Access.ReadBuffers[i].ID;
                FLironPassResourceBufferState& CurrentState = BufferTracker[BufID];
            
                // Если этот буфер используется впервые в кадре или стейт изменился
                // (Например, в прошлом кадре в него писали, а сейчас мы из него читаем)
                FErolyssaBarrierResourceState TargetState = FErolyssaBarrierResourceState::ShaderRead;
                if (CurrentState.State != TargetState)
                {
                    VkPipelineStageFlags2 OldStage, NewStage;
                    VkAccessFlags2        OldAccess, NewAccess;
                
                    ToVulkan(CurrentState.State, OldStage, OldAccess);
                    ToVulkan(TargetState, NewStage, NewAccess);

                    VkBufferMemoryBarrier2 Barrier{};
                    Barrier.sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    Barrier.srcStageMask  = OldStage;
                    Barrier.srcAccessMask = OldAccess;
                    Barrier.dstStageMask  = NewStage;
                    Barrier.dstAccessMask = NewAccess;
                    Barrier.buffer        = BufferTracker[BufID].RealBuffer; 
                    Barrier.offset        = 0;
                    Barrier.size          = VK_WHOLE_SIZE;

                    BufferBarriers.push_back(Barrier);
                    CurrentState.State = TargetState; // Обновляем глобальный трекер
                }
                BufferTouched[BufID] = true;
            }

            // Обрабатываем буферы записи (WriteBuffers)
            for (size_t i = 0; i < Pass.Access.WriteBuffers.Num(); ++i)
            {
                uint32 BufID = Pass.Access.WriteBuffers[i].ID;
                FLironPassResourceBufferState& CurrentState = BufferTracker[BufID];
            
                FErolyssaBarrierResourceState TargetState = FErolyssaBarrierResourceState::ShaderWrite;
                if (CurrentState.State != TargetState)
                {
                    VkPipelineStageFlags2 OldStage, NewStage;
                    VkAccessFlags2        OldAccess, NewAccess;
                
                    ToVulkan(CurrentState.State, OldStage, OldAccess);
                    ToVulkan(TargetState, NewStage, NewAccess);

                    VkBufferMemoryBarrier2 Barrier{};
                    Barrier.sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    Barrier.srcStageMask  = OldStage;
                    Barrier.srcAccessMask = OldAccess;
                    Barrier.dstStageMask  = NewStage;
                    Barrier.dstAccessMask = NewAccess;
                    Barrier.buffer        = BufferTracker[BufID].RealBuffer; 
                    Barrier.offset        = 0;
                    Barrier.size          = VK_WHOLE_SIZE;

                    BufferBarriers.push_back(Barrier);
                    CurrentState.State = TargetState;
                }
                BufferTouched[BufID] = true;
            }

            // =====================================================================
            // 2. СИНХРОНИЗАЦИЯ ТЕКСТУР (Read & Write)
            // =====================================================================
        
            // Обрабатываем текстуры чтения (ReadTextures)
            for (size_t i = 0; i < Pass.Access.ReadTextures.Num(); ++i)
            {
                uint32 TexID = Pass.Access.ReadTextures[i].ID;
                FLironPassResourceTextureState& CurrentState = TextureTracker[TexID];

                FErolyssaBarrierResourceState TargetState = FErolyssaBarrierResourceState::ShaderRead;
                if (CurrentState.State != TargetState)
                {
                    VkPipelineStageFlags2 OldStage, NewStage;
                    VkAccessFlags2        OldAccess, NewAccess;
                    VkImageLayout         OldLayout, NewLayout;

                    ToVulkan(CurrentState.State, OldStage, OldAccess, &OldLayout);
                    ToVulkan(TargetState, NewStage, NewAccess, &NewLayout);

                    VkImageMemoryBarrier2 Barrier{};
                    Barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    Barrier.srcStageMask  = OldStage;
                    Barrier.srcAccessMask = OldAccess;
                    Barrier.oldLayout     = OldLayout;
                    Barrier.dstStageMask  = NewStage;
                    Barrier.dstAccessMask = NewAccess;
                    Barrier.newLayout     = NewLayout;
                    Barrier.image         = TextureTracker[TexID].RealImage;
                    Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // Для глубины нужен DEPTH_BIT
                    Barrier.subresourceRange.levelCount = 1;
                    Barrier.subresourceRange.layerCount = 1;

                    ImageBarriers.push_back(Barrier);
                    CurrentState.State = TargetState;
                }
                TextureTouched[TexID] = true;
            }

            // Обрабатываем текстуры записи (WriteTextures — Таргеты рендеринга)
            bool IsGraphicsPass = (Pass.Access.WriteTextures.Num() > 0);
        
            // Переменная, которая будет хранить режим очистки для Vulkan Dynamic Rendering
            VkAttachmentLoadOp CurrentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

            for (size_t i = 0; i < Pass.Access.WriteTextures.Num(); ++i)
            {
                uint32 TexID = Pass.Access.WriteTextures[i].ID;
                FLironPassResourceTextureState& CurrentState = TextureTracker[TexID];

                // Определяем, куда мы пишем (в Swapchain/Цвет или в буфер Глубины)
                // В идеале сделать проверку формата, пока по умолчанию ставим Color
                FErolyssaBarrierResourceState TargetState = FErolyssaBarrierResourceState::ColorAttachment;

                // АВТОМАТИЧЕСКИЙ ВЫБОР LOAD_OP (Вся суть жонглирования):
                // Если в этом кадре мы УЖЕ писали/читали эту текстуру — мы обязаны сохранить данные (LOAD).
                // Если текстура трогается впервые в кадре — мы её очищаем (CLEAR).
                if (TextureTouched[TexID])
                {
                    CurrentLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                }

                if (CurrentState.State != TargetState)
                {
                    VkPipelineStageFlags2 OldStage, NewStage;
                    VkAccessFlags2        OldAccess, NewAccess;
                    VkImageLayout         OldLayout, NewLayout;
                    
                    ToVulkan(CurrentState.State, OldStage, OldAccess, &OldLayout);
                    ToVulkan(TargetState, NewStage, NewAccess, &NewLayout);
                    
                    VkImageMemoryBarrier2 Barrier{};
                    Barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    Barrier.srcStageMask  = OldStage;
                    Barrier.srcAccessMask = OldAccess;
                    Barrier.oldLayout     = OldLayout;
                    Barrier.dstStageMask  = NewStage;
                    Barrier.dstAccessMask = NewAccess;
                    Barrier.newLayout     = NewLayout;
                    Barrier.image         = TextureTracker[TexID].RealImage;
                    Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    Barrier.subresourceRange.levelCount = 1;
                    Barrier.subresourceRange.layerCount = 1;

                    ImageBarriers.push_back(Barrier);
                    CurrentState.State = TargetState;
                }
                TextureTouched[TexID] = true;
            }

            // =====================================================================
            // 3. ОТПРАВКА БАРЬЕРОВ В ВУЛКАН (Схлопываем пачкой через Sync 2)
            // =====================================================================
            if (!ImageBarriers.empty() || !BufferBarriers.empty())
            {
                VkDependencyInfo DependencyInfo{};
                DependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                DependencyInfo.imageMemoryBarrierCount  = static_cast<uint32_t>(ImageBarriers.size());
                DependencyInfo.pImageMemoryBarriers    = ImageBarriers.data();
                DependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(BufferBarriers.size());
                DependencyInfo.pBufferMemoryBarriers    = BufferBarriers.data();
                
                vkCmdPipelineBarrier2(InCommandBuffer, &DependencyInfo);
            }

            // =====================================================================
            // 4. ОТКРЫТИЕ ПАССА И ЗАПУСК ЛЯМБДЫ
            // =====================================================================
            if (IsGraphicsPass)
            {
                // Локальный массив для аттачментов цвета. Максимум 4 согласно лимитам FLironAccess
                VkRenderingAttachmentInfo ColorAttachments[4]{};
                uint32 RenderWidth = 0;
                uint32 RenderHeight = 0;

                for (size_t i = 0; i < Pass.Access.WriteTextures.Num(); ++i)
                {
                    uint32 TexID = Pass.Access.WriteTextures[i].ID;
                    
                    VkImageView RealView = TextureTracker[TexID].RealView; 
                    
                    ColorAttachments[i].sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    ColorAttachments[i].imageView   = RealView;
                    ColorAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    ColorAttachments[i].loadOp      = CurrentLoadOp;
                    ColorAttachments[i].storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
                
                    // Значения очистки экрана (Clear Value). Для простоты пока ставим черный цвет
                    ColorAttachments[i].clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
                }

                VkRenderingInfo RenderingInfo{};
                RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                RenderingInfo.renderArea.offset = { 0, 0 };
                RenderingInfo.renderArea.extent = { RenderWidth, RenderHeight };
                RenderingInfo.layerCount = 1;
                RenderingInfo.colorAttachmentCount = static_cast<uint32_t>(Pass.Access.WriteTextures.Num());
                RenderingInfo.pColorAttachments = ColorAttachments;
            
                // Если в будущем добавишь пассы с глубиной, сюда нужно будет дописать 
                // заполнение RenderingInfo.pDepthAttachment
                RenderingInfo.pDepthAttachment = nullptr; 
                RenderingInfo.pStencilAttachment = nullptr;

                vkCmdBeginRendering(InCommandBuffer, &RenderingInfo);
            }
        
            Pass.ExecuteLambda(InCommandBuffer);
        
            if (IsGraphicsPass)
            {
                vkCmdEndRendering(InCommandBuffer);
            }
        }
        
        Clear();
    }
    */
    
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
