/// @file RenderGraph.h
/// @brief Declarative pass DAG with auto barriers + transient image allocation.
///
/// Construct one per frame. Declare your textures + passes (each with its
/// `Read`/`Write`/`Color`/`Depth` accesses); the graph inserts the right
/// `TransitionFor*` calls before each pass and opens dynamic-rendering scopes
/// for graphics passes automatically. Call `Present(handle)` to display a
/// final-pass output and the graph blits it to the swapchain at the end.
///
/// V1 executes passes in declaration order — no topo sort, no dead-pass
/// elimination, no transient memory aliasing. Order your declarations so a
/// pass that reads X comes after the pass that writes X. Phase 13 polish
/// adds true graph optimization.
#pragma once

#include <RHI/Public/CommandList.h>
#include <RHI/Public/Texture.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace helio::rhi { class Device; }

namespace helio::render {

using rhi::CommandList;
using rhi::Device;
using rhi::Format;
using rhi::TextureHandle;
using rhi::TextureUsage;

enum class PassKind : uint8_t { Graphics, Compute };

enum class Access : uint8_t {
    Read,    ///< Shader-sample (transitions to SHADER_READ_ONLY_OPTIMAL).
    Write,   ///< Compute storage-image write (transitions to GENERAL).
    Color,   ///< Graphics color attachment (handled by BeginRendering).
    Depth,   ///< Graphics depth attachment (handled by BeginRendering).
};

struct ResourceUse {
    TextureHandle Handle;
    Access Mode;
    float ClearColor[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float ClearDepth{0.0f};
    bool  ClearOnLoad{true};
};

struct Pass {
    std::string Name;
    PassKind Kind{PassKind::Graphics};
    std::vector<ResourceUse> Uses;
    std::function<void(CommandList&)> Fn;
};

class RenderGraph;

/// Fluent builder returned by `RenderGraph::Graphics()` / `Compute()`.
/// Calls chain through one another and finish with `Execute(...)`.
class PassBuilder {
public:
    PassBuilder& Read(TextureHandle h);
    PassBuilder& Write(TextureHandle h);

    
    PassBuilder& Color(TextureHandle h);
    PassBuilder& Color(TextureHandle h, float r, float g, float b, float a);
    
    /// Color attachment whose existing contents are preserved (LoadOp::Load).
    /// Use when drawing on top of a previous pass's output (e.g. the overlay
    /// pass renders over the scene without clearing it).
    PassBuilder& ColorLoad(TextureHandle h);
    
    PassBuilder& Depth(TextureHandle h, float ClearDepth = 0.0f);
    
    PassBuilder& DepthLoad(TextureHandle h);
    /// Set the user callback for this pass. Required.
    void Execute(std::function<void(CommandList&)> Fn);

private:
    friend class RenderGraph;
    PassBuilder(RenderGraph& G, uint32_t PassIdx) : m_graph(&G), m_pass(PassIdx) {}
    RenderGraph* m_graph;
    uint32_t m_pass;
};

class RenderGraph {
public:
    /// Construct against a live frame's CommandList (from `Device::BeginFrame`).
    RenderGraph(Device& Dev, CommandList& Cmd);

    /// Auto-executes pending passes if `Execute()` wasn't called explicitly,
    /// then destroys transient textures via the device's deletion queue.
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    /// Allocate a transient texture for this graph's lifetime. The texture
    /// gets a real `TextureHandle` (with bindless slots etc.) and is queued
    /// for destruction when the graph executes / destructs.
    /// `Usage` is what the user wants on top of what the graph will infer
    /// from declared accesses (graph adds ColorAttachment/Storage as needed).
    [[nodiscard]] TextureHandle Image(const char* Name,
                                      uint32_t Width, uint32_t Height,
                                      Format Fmt,
                                      TextureUsage Usage = TextureUsage::Sampled
                                                         | TextureUsage::ColorAttachment);

    /// Open a graphics pass. Returns a builder — chain `.Color()` / `.Depth()`
    /// / `.Read()` / `.Execute()` on it.
    [[nodiscard]] PassBuilder Graphics(const char* Name);

    /// Open a compute pass. Chain `.Read()` / `.Write()` / `.Execute()`.
    [[nodiscard]] PassBuilder Compute(const char* Name);

    /// Mark `Src` as this graph's final output. The graph blits it to the
    /// current swapchain image at execution end. Call after all passes have
    /// been declared.
    void Present(TextureHandle Src);

    /// Run all declared passes against the bound CommandList. Idempotent —
    /// the destructor calls this if you don't.
    void Execute();

    /// Number of passes declared so far. Useful for stats overlays (e.g.
    /// `Overlay::DrawStats(.., .., rg.Passes())`).
    [[nodiscard]] uint32_t Passes() const noexcept {
        return static_cast<uint32_t>(m_passes.size());
    }

private:
    friend class PassBuilder;
    void ExecutePass(const Pass& P);

    Device* m_dev{nullptr};
    CommandList* m_cmd{nullptr};
    std::vector<Pass> m_passes;
    std::vector<TextureHandle> m_transients;
    TextureHandle m_presentSrc{};
    bool m_executed{false};
};

} // namespace helio::render
