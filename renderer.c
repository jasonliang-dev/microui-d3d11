#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include "renderer.h"
#include "atlas.inl"
#include <d3d11.h>
#include <d3dcompiler.h>

#define BUFFER_SIZE 8192

typedef struct {
  IDXGISwapChain *swap_chain;
  ID3D11Device *device;
  ID3D11DeviceContext *ctx;
  ID3D11Texture2D *rt;
  ID3D11RenderTargetView *rtv;

  int backbuf_width;
  int backbuf_height;
} GPU;

typedef struct {
  float vert[2];
  float tex[2];
  unsigned char col[4];
} Vertex;

typedef struct {
  int offset; /* vertex buffer start offset */
  int cursor; /* total number of vertices in the buffer */

  Vertex vertices[BUFFER_SIZE];

  ID3D11Texture2D *atlas_tex;
  ID3D11ShaderResourceView *atlas_srv;
  ID3D11SamplerState *sampler;
  ID3D11RasterizerState *rasterizer;
  ID3D11BlendState *blend;

  ID3D11Buffer *vbuf;
  ID3D11Buffer *ibuf;
  ID3D11Buffer *cbuf;

  ID3D11InputLayout *input_layout;
  ID3D11VertexShader *vs;
  ID3D11PixelShader *ps;
} Renderer;

static GPU gpu;
static HWND hwnd;
static Renderer renderer;
static mu_Context *mui_ctx;

void r_init(WindowsMessageCallback wndproc) {
  /* init window */

  const char *title = "";

  RegisterClassA(&(WNDCLASSA){
      .lpfnWndProc = (WNDPROC)wndproc,
      .hInstance = NULL,
      .hCursor = LoadCursor(NULL, IDC_ARROW),
      .lpszClassName = "microui-d3d11",
  });

  int width = 800, height = 600;
  hwnd = CreateWindowExA(0, "microui-d3d11", "", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width,
                         height, NULL, NULL, NULL, NULL);

  /* init device and swap chain */

  D3D11CreateDeviceAndSwapChain(0, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                (D3D_FEATURE_LEVEL[]){D3D_FEATURE_LEVEL_11_0}, 1, D3D11_SDK_VERSION,
                                &(DXGI_SWAP_CHAIN_DESC){
                                    .BufferCount = 2,
                                    .BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
                                    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                    .OutputWindow = hwnd,
                                    .SampleDesc.Count = 1,
                                    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                    .Windowed = 1,
                                },
                                &gpu.swap_chain, &gpu.device, 0, &gpu.ctx);

  /* init atlas */

  {
    D3D11_TEXTURE2D_DESC desc = {
        .Width = ATLAS_WIDTH,
        .Height = ATLAS_HEIGHT,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R8_UNORM,
        .SampleDesc.Count = 1,
        .SampleDesc.Quality = 0,
        .Usage = D3D11_USAGE_IMMUTABLE,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };

    D3D11_SUBRESOURCE_DATA sr_data = {.pSysMem = atlas_texture, .SysMemPitch = ATLAS_WIDTH};

    ID3D11Device_CreateTexture2D(gpu.device, &desc, &sr_data, &renderer.atlas_tex);
    ID3D11Device_CreateShaderResourceView(gpu.device, (ID3D11Resource *)renderer.atlas_tex, NULL,
                                          &renderer.atlas_srv);
  }

  /* init effect states */

  {
    D3D11_SAMPLER_DESC desc = {
        .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
        .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
        .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
        .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
    };

    ID3D11Device_CreateSamplerState(gpu.device, &desc, &renderer.sampler);
  }

  {
    D3D11_RASTERIZER_DESC desc = {
        .FillMode = D3D11_FILL_SOLID,
        .CullMode = D3D11_CULL_BACK,
        .ScissorEnable = 1,
    };

    ID3D11Device_CreateRasterizerState(gpu.device, &desc, &renderer.rasterizer);
  }

  {
    D3D11_BLEND_DESC desc = {
        .RenderTarget[0] =
            {
                .BlendEnable = 1,
                .SrcBlend = D3D11_BLEND_SRC_ALPHA,
                .DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
                .BlendOp = D3D11_BLEND_OP_ADD,
                .SrcBlendAlpha = D3D11_BLEND_ONE,
                .DestBlendAlpha = D3D11_BLEND_ZERO,
                .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
            },
    };

    ID3D11Device_CreateBlendState(gpu.device, &desc, &renderer.blend);
  }

  /* init buffers */

  {
    D3D11_BUFFER_DESC desc = {
        .ByteWidth = sizeof(renderer.vertices),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
    };

    ID3D11Device_CreateBuffer(gpu.device, &desc, NULL, &renderer.vbuf);
  }

  {
    unsigned short indices[BUFFER_SIZE * 6 / 4];

    for (int i = 0, v = 0; i < BUFFER_SIZE * 6 / 4; i += 6, v += 4) {
      indices[i + 0] = v + 0;
      indices[i + 1] = v + 2;
      indices[i + 2] = v + 3;

      indices[i + 3] = v + 2;
      indices[i + 4] = v + 0;
      indices[i + 5] = v + 1;
    }

    D3D11_BUFFER_DESC desc = {
        .ByteWidth = sizeof(indices),
        .Usage = D3D11_USAGE_IMMUTABLE,
        .BindFlags = D3D11_BIND_INDEX_BUFFER,
    };

    D3D11_SUBRESOURCE_DATA data = {.pSysMem = indices};
    ID3D11Device_CreateBuffer(gpu.device, &desc, &data, &renderer.ibuf);
  }

  {
    D3D11_BUFFER_DESC desc = {
        .ByteWidth = 4 * 4 * sizeof(float),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
    };

    ID3D11Device_CreateBuffer(gpu.device, &desc, NULL, &renderer.cbuf);
  }

  /* init vertex and pixel shader */

  const char *hlsl = "struct VertexIn {"
                     "  float2 position: POS;"
                     "  float2 texcoord: TEX;"
                     "  float4 color: COL;"
                     "};"

                     "struct VertexOut {"
                     "  float4 position: SV_POSITION;"
                     "  float2 texcoord: TEX;"
                     "  float4 color: COL;"
                     "};"

                     "cbuffer cbuffer0: register(b0) {"
                     "  float4x4 u_projection;"
                     "}"

                     "Texture2D<float4> texture0: register(t0);"
                     "SamplerState sampler0: register(s0);"

                     "VertexOut vs_main(VertexIn input) {"
                     "  VertexOut output;"
                     "  output.position = mul(u_projection, float4(input.position, 0.0, 1.0));"
                     "  output.texcoord = input.texcoord;"
                     "  output.color = input.color;"
                     "  return output;"
                     "}"

                     "float4 ps_main(VertexOut input): SV_TARGET {"
                     "  float alpha = texture0.Sample(sampler0, input.texcoord).r;"
                     "  return input.color * float4(1.0, 1.0, 1.0, alpha);"
                     "}";

  HINSTANCE dll = LoadLibraryA("d3dcompiler_47.dll");
  pD3DCompile d3d_compile_proc = (pD3DCompile)GetProcAddress(dll, "D3DCompile");
#define D3DCompile d3d_compile_proc

  UINT compile_flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS |
                       D3DCOMPILE_WARNINGS_ARE_ERRORS;

  ID3DBlob *vs_blob = NULL;
  D3DCompile(hlsl, strlen(hlsl), NULL, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vs_main", "vs_5_0",
             compile_flags, 0, &vs_blob, NULL);
  ID3D11Device_CreateVertexShader(gpu.device, ID3D10Blob_GetBufferPointer(vs_blob),
                                  ID3D10Blob_GetBufferSize(vs_blob), NULL, &renderer.vs);

  ID3DBlob *ps_blob = NULL;
  D3DCompile(hlsl, strlen(hlsl), NULL, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "ps_main", "ps_5_0",
             compile_flags, 0, &ps_blob, NULL);
  ID3D11Device_CreatePixelShader(gpu.device, ID3D10Blob_GetBufferPointer(ps_blob),
                                 ID3D10Blob_GetBufferSize(ps_blob), NULL, &renderer.ps);

  {
    D3D11_INPUT_ELEMENT_DESC desc[] = {
        {.SemanticName = "POS",
         .Format = DXGI_FORMAT_R32G32_FLOAT,
         .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA},
        {.SemanticName = "TEX",
         .Format = DXGI_FORMAT_R32G32_FLOAT,
         .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT,
         .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA},
        {.SemanticName = "COL",
         .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
         .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT,
         .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA},
    };

    ID3D11Device_CreateInputLayout(gpu.device, desc, ARRAYSIZE(desc),
                                   ID3D10Blob_GetBufferPointer(vs_blob),
                                   ID3D10Blob_GetBufferSize(vs_blob), &renderer.input_layout);
  }

  /* end init */

  ShowWindow(hwnd, 1);
}

static void flush(void) {
  ID3D11DeviceContext *ctx = gpu.ctx;
  int count = renderer.cursor - renderer.offset;

  /* first flush this frame or buffer overflowed, when renderer.offset == 0 */
  D3D11_MAP map_write =
      renderer.offset == 0 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;

  /* send vertex data to vram. */
  D3D11_MAPPED_SUBRESOURCE sr = {0};
  ID3D11DeviceContext_Map(gpu.ctx, (ID3D11Resource *)renderer.vbuf, 0, map_write, 0, &sr);
  Vertex *data = sr.pData;
  memcpy(data + renderer.offset, renderer.vertices + renderer.offset, count * sizeof(Vertex));
  ID3D11DeviceContext_Unmap(gpu.ctx, (ID3D11Resource *)renderer.vbuf, 0);

  /* draw the copied data */
  ID3D11DeviceContext_DrawIndexed(gpu.ctx, count * 6 / 4, renderer.offset * 6 / 4, 0);
  renderer.offset = renderer.cursor;
}

static void push_quad(mu_Rect dst, mu_Rect src, mu_Color color) {
  if (renderer.cursor == BUFFER_SIZE) {
    flush();
    renderer.offset = 0;
    renderer.cursor = 0;
  }

  float x0 = dst.x;
  float y0 = dst.y;
  float x1 = dst.x + dst.w;
  float y1 = dst.y + dst.h;

  float u0 = src.x / (float)ATLAS_WIDTH;
  float v0 = src.y / (float)ATLAS_HEIGHT;
  float u1 = (src.x + src.w) / (float)ATLAS_WIDTH;
  float v1 = (src.y + src.h) / (float)ATLAS_HEIGHT;

  unsigned char r = color.r;
  unsigned char g = color.g;
  unsigned char b = color.b;
  unsigned char a = color.a;

  renderer.vertices[renderer.cursor + 0] = (Vertex){{x0, y0}, {u0, v0}, {r, g, b, a}};
  renderer.vertices[renderer.cursor + 1] = (Vertex){{x1, y0}, {u1, v0}, {r, g, b, a}};
  renderer.vertices[renderer.cursor + 2] = (Vertex){{x1, y1}, {u1, v1}, {r, g, b, a}};
  renderer.vertices[renderer.cursor + 3] = (Vertex){{x0, y1}, {u0, v1}, {r, g, b, a}};

  renderer.cursor += 4;
}

void r_draw_rect(mu_Rect rect, mu_Color color) { push_quad(rect, atlas[ATLAS_WHITE], color); }

void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color) {
  mu_Rect dst = {pos.x, pos.y, 0, 0};
  for (const char *p = text; *p; p++) {
    if ((*p & 0xc0) == 0x80) {
      continue;
    }
    int chr = mu_min((unsigned char)*p, 127);
    mu_Rect src = atlas[ATLAS_FONT + chr];
    dst.w = src.w;
    dst.h = src.h;
    push_quad(dst, src, color);
    dst.x += dst.w;
  }
}

void r_draw_icon(int id, mu_Rect rect, mu_Color color) {
  mu_Rect src = atlas[id];
  int x = rect.x + (rect.w - src.w) / 2;
  int y = rect.y + (rect.h - src.h) / 2;
  push_quad(mu_rect(x, y, src.w, src.h), src, color);
}

int r_get_text_width(const char *text, int len) {
  int res = 0;
  for (const char *p = text; *p && len--; p++) {
    if ((*p & 0xc0) == 0x80) {
      continue;
    }
    int chr = mu_min((unsigned char)*p, 127);
    res += atlas[ATLAS_FONT + chr].w;
  }
  return res;
}

int r_get_text_height(void) { return 18; }

void r_set_clip_rect(mu_Rect rect) {
  flush();

  D3D11_RECT scissor = {rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
  ID3D11DeviceContext_RSSetScissorRects(gpu.ctx, 1, &scissor);
}

void r_clear(mu_Color color) {
  renderer.offset = 0;
  renderer.cursor = 0;

  RECT rect = {0};
  GetClientRect(hwnd, &rect);
  int width = rect.right - rect.left;
  int height = rect.bottom - rect.top;

  /* resize swap chain if necessary */
  if (width != gpu.backbuf_width || height != gpu.backbuf_height) {
    gpu.backbuf_width = width;
    gpu.backbuf_height = height;

    if (gpu.rt) {
      ID3D11Texture2D_Release(gpu.rt);
      ID3D11RenderTargetView_Release(gpu.rtv);
    }

    IDXGISwapChain_ResizeBuffers(gpu.swap_chain, 0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    IDXGISwapChain_GetBuffer(gpu.swap_chain, 0, &IID_ID3D11Texture2D, (void **)&gpu.rt);
    ID3D11Device_CreateRenderTargetView(gpu.device, (ID3D11Resource *)gpu.rt, 0, &gpu.rtv);
  }

  float rl = 1.0f / width;
  float tb = 1.0f / -height;

  /* orthographic projection */
  float projection[4][4] = {0};
  projection[0][0] = 2.0f * rl;
  projection[1][1] = 2.0f * tb;
  projection[2][2] = -1.0f;
  projection[3][0] = -width * rl;
  projection[3][1] = -height * tb;
  projection[3][3] = 1.0f;

  D3D11_MAPPED_SUBRESOURCE sr = {0};
  ID3D11DeviceContext_Map(gpu.ctx, (ID3D11Resource *)renderer.cbuf, 0, D3D11_MAP_WRITE_DISCARD, 0,
                          &sr);
  memcpy(sr.pData, projection, 4 * 4 * sizeof(float));
  ID3D11DeviceContext_Unmap(gpu.ctx, (ID3D11Resource *)renderer.cbuf, 0);

  /* start render pass */
  float bg_color[4] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
  UINT stride = sizeof(Vertex);
  UINT offset = 0;
  D3D11_VIEWPORT viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
  D3D11_RECT scissor = {0, 0, width, height};
  ID3D11DeviceContext_ClearRenderTargetView(gpu.ctx, gpu.rtv, bg_color);
  ID3D11DeviceContext_IASetPrimitiveTopology(gpu.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_IASetInputLayout(gpu.ctx, renderer.input_layout);
  ID3D11DeviceContext_IASetVertexBuffers(gpu.ctx, 0, 1, &renderer.vbuf, &stride, &offset);
  ID3D11DeviceContext_IASetIndexBuffer(gpu.ctx, renderer.ibuf, DXGI_FORMAT_R16_UINT, 0);
  ID3D11DeviceContext_VSSetConstantBuffers(gpu.ctx, 0, 1, &renderer.cbuf);
  ID3D11DeviceContext_VSSetShader(gpu.ctx, renderer.vs, NULL, 0);
  ID3D11DeviceContext_RSSetViewports(gpu.ctx, 1, &viewport);
  ID3D11DeviceContext_RSSetState(gpu.ctx, renderer.rasterizer);
  ID3D11DeviceContext_RSSetScissorRects(gpu.ctx, 1, &scissor);
  ID3D11DeviceContext_PSSetSamplers(gpu.ctx, 0, 1, &renderer.sampler);
  ID3D11DeviceContext_PSSetShaderResources(gpu.ctx, 0, 1, &renderer.atlas_srv);
  ID3D11DeviceContext_PSSetShader(gpu.ctx, renderer.ps, NULL, 0);
  ID3D11DeviceContext_OMSetBlendState(gpu.ctx, renderer.blend, NULL, ~0U);
  ID3D11DeviceContext_OMSetDepthStencilState(gpu.ctx, NULL, 0);
  ID3D11DeviceContext_OMSetRenderTargets(gpu.ctx, 1, &gpu.rtv, NULL);
}

void r_present(void) {
  flush();
  IDXGISwapChain_Present(gpu.swap_chain, 1, 0);
}
