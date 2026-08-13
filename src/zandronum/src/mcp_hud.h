//
// mcp_hud.h -- capture what the engine paints on screen. Part of the native MCP bridge
// (features/mcp-bridge); compiled only when FUA_MCP_BRIDGE is ON. When OFF, the draw-funnel anchors
// in v_text.cpp / v_draw.cpp call the inline no-ops below, so release builds link with no bridge.
//
#ifndef MCP_HUD_H
#define MCP_HUD_H

class FTexture;

#ifdef FUA_MCP_BRIDGE

// Tees, called from the engine's draw funnels (anchored inserts in v_text.cpp / v_draw.cpp).
void MCP_HUD_TeeText( int x, int y, const char *string );
void MCP_HUD_TeeTexture( double x, double y, FTexture *img );

// Snapshot the frame just drawn (called once per frame from the RPC tick).
void MCP_HUD_BeginFrame();

#else

inline void MCP_HUD_TeeText( int, int, const char * ) {}
inline void MCP_HUD_TeeTexture( double, double, FTexture * ) {}
inline void MCP_HUD_BeginFrame() {}

#endif // FUA_MCP_BRIDGE

#endif
