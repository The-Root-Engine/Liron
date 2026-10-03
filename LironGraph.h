// Root Engine / Liron

#pragma once

#include "Liron.h"
#include "LironTatemae.h"
#include "LironStructures.h"

#include <vector>
#include <string>
#include <functional>
#include <algorithm>

class FLironGraph 
{
    
public:
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
    
    FLironTextureHandle ImportTexture(const std::string_view InName, VkImage InImage, VkImageView InView, VkExtent2D InExtent)
    {
        const uint32 NewID = TextureTracker.Num();
        
#if LIRON_ENABLE_DEBUG
        TextureNames.Emplace(InName);
#endif
        
        FLironPassResourceTextureState ResourceTextureState{};
        ResourceTextureState.State         = FErolyssaBarrierResourceState::None; 
        ResourceTextureState.RealImage     = InImage;
        ResourceTextureState.RealView      = InView;
        ResourceTextureState.Extent        = InExtent;
        ResourceTextureState.bIsPersistent = true;
        
        TextureTracker.Add(ResourceTextureState);
        return FLironTextureHandle(NewID);
    }
    
    FLironBufferHandle ImportBuffer(const std::string_view InName, const VkBuffer InBuffer)
    {
        const uint32 NewID = BufferTracker.Num();
        
#if LIRON_ENABLE_DEBUG
        BufferNames.Add(std::string(InName));
#endif
        
        FLironPassResourceBufferState ResourceBufferState{};
        ResourceBufferState.State         = FErolyssaBarrierResourceState::None;
        ResourceBufferState.RealBuffer    = InBuffer;
        ResourceBufferState.bIsPersistent = true;
        
        BufferTracker.Add(ResourceBufferState);
        return FLironBufferHandle(NewID);
    }
    
    void AddPass(const std::string_view InName, const FLironPassAccess& InAccess, const int32 InPriority, const std::function<void(const FErolyssaCommandBuffer& CommandBuffer)>& InExecuteLambda)
    {
        FLironPass Pass{};
        
#if LIRON_ENABLE_DEBUG
        Pass.Name = InName;
#endif
        
        Pass.Access = InAccess;
        Pass.Priority = InPriority;
        Pass.ExecuteLambda = InExecuteLambda;
        
        RegisteredPasses.Emplace(Pass);
    }
    
    /*
    void Execute()
    {
        TArray<uint32> ExecutionOrder;
        BuildExecutionOrder(ExecutionOrder);
        
        for(const uint32 PassIndex : ExecutionOrder)
            if(const FLironPass& Pass = RegisteredPasses[PassIndex]; Pass.ExecuteLambda)
                Pass.ExecuteLambda();
        
        Clear();
    }
    */
    
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
            const FLironPass& Pass = RegisteredPasses[PassIndex];
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
    
    void Clear()
    {
        RegisteredPasses.Empty();
        
        TextureTracker.Empty();
        BufferTracker.Empty();
        
#if LIRON_ENABLE_DEBUG
        TextureNames.Empty();
        BufferNames.Empty();
#endif
    }

private:
    TArray<FLironPass> RegisteredPasses;
    
#if LIRON_ENABLE_DEBUG
    TArray<std::string> TextureNames;
    TArray<std::string> BufferNames;
#endif
    
    TArray<FLironPassResourceTextureState> TextureTracker;
    TArray<FLironPassResourceBufferState > BufferTracker;
    
    enum class EPassColor : uint8 { White, Gray, Black };
    
    bool PassWritesToTexture(const uint32 PassIndex, const uint32 TextureID) const
    {
        for(const FLironTextureHandle& TextureHandle : RegisteredPasses[PassIndex].Access.WriteTextures)
            if(TextureHandle.ID == TextureID) return true;
        
        return false;
    }
    
    bool PassWritesToBuffer(const uint32 PassIndex, const uint32 BufferID) const
    {
        for(const FLironBufferHandle& BufferHandle : RegisteredPasses[PassIndex].Access.WriteBuffers)
            if(BufferHandle.ID == BufferID) return true;
        
        return false;
    }
    
    void BuildExecutionOrder(TArray<uint32>& OutOrder)
    {
        std::vector<EPassColor> PassColors(RegisteredPasses.Num(), EPassColor::White);
        OutOrder.Empty();
        
        std::function<void(uint32)> VisitPass = [&](uint32 PassIndex) {
            if (PassColors[PassIndex] == EPassColor::Black) return;
            check(PassColors[PassIndex] != EPassColor::Gray, "Cyclic dependency detected!");

            PassColors[PassIndex] = EPassColor::Gray;
            const auto& CurrentPass = RegisteredPasses[PassIndex];
            
            // ---------------------------------------------------------------------
            // 1. СВЯЗЬ ПО ЧТЕНИЮ (RaW): Ищем, кто писал в то, что мы читаем
            // ---------------------------------------------------------------------
            
            // Проверка зависимостей по ТЕКСТУРАМ
            for (size_t i = 0; i < CurrentPass.Access.ReadTextures.Num(); ++i) {
                uint32 ReadTexID = CurrentPass.Access.ReadTextures[i].ID;
                for (uint32 PrevIdx = 0; PrevIdx < RegisteredPasses.Num(); ++PrevIdx) {
                    if (PrevIdx == PassIndex) continue;
                    if (PassWritesToTexture(PrevIdx, ReadTexID)) {
                        VisitPass(PrevIdx);
                    }
                }
            }

            // Проверка зависимостей по БУФЕРАМ
            for (size_t i = 0; i < CurrentPass.Access.ReadBuffers.Num(); ++i) {
                uint32 ReadBufID = CurrentPass.Access.ReadBuffers[i].ID;
                for (uint32 PrevIdx = 0; PrevIdx < RegisteredPasses.Num(); ++PrevIdx) {
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
                for (uint32 PrevIdx = 0; PrevIdx < RegisteredPasses.Num(); ++PrevIdx) {
                    if (PrevIdx == PassIndex) continue;
                    
                    if (PassWritesToTexture(PrevIdx, WriteTexID)) {
                        if (RegisteredPasses[PrevIdx].Priority < CurrentPass.Priority) {
                            VisitPass(PrevIdx);
                        }
                    }
                }
            }

            // Разруливаем WaW конфликты для БУФЕРАВ (например, два вычислительных пасса пишут в один буфер аргументов)
            for (size_t i = 0; i < CurrentPass.Access.WriteBuffers.Num(); ++i) {
                uint32 WriteBufID = CurrentPass.Access.WriteBuffers[i].ID;
                for (uint32 PrevIdx = 0; PrevIdx < RegisteredPasses.Num(); ++PrevIdx) {
                    if (PrevIdx == PassIndex) continue;
                    
                    if (PassWritesToBuffer(PrevIdx, WriteBufID)) {
                        if (RegisteredPasses[PrevIdx].Priority < CurrentPass.Priority) {
                            VisitPass(PrevIdx);
                        }
                    }
                }
            }
            
            PassColors[PassIndex] = EPassColor::Black;
            OutOrder.Add(PassIndex);
        };
        
        std::vector<uint32> SortedIndices(RegisteredPasses.Num());
        for (uint32 i = 0; i < RegisteredPasses.Num(); ++i) SortedIndices[i] = i;
        
        std::sort(SortedIndices.begin(), SortedIndices.end(), [this](uint32 a, uint32 b) {
            return RegisteredPasses[a].Priority > RegisteredPasses[b].Priority;
        });
        
        for (uint32 i : SortedIndices) {
            if (PassColors[i] == EPassColor::White) VisitPass(i);
        }
    }
};
