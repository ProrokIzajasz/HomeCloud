#include "homecloud/image_variant.hpp"

#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

using namespace std;
using Microsoft::WRL::ComPtr;

namespace homecloud {
namespace {

class ComApartment {
public:
    ComApartment() {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = result == S_OK || result == S_FALSE;
        available_ = initialized_ || result == RPC_E_CHANGED_MODE;
    }
    ~ComApartment() { if (initialized_) CoUninitialize(); }
    [[nodiscard]] bool available() const noexcept { return available_; }
private:
    bool initialized_{};
    bool available_{};
};

bool succeeded(HRESULT result) { return SUCCEEDED(result); }

} // namespace

optional<ImageVariant> create_jpeg_variant(const filesystem::path& source,
                                            uint32_t maximum_width,
                                            uint32_t maximum_height,
                                            float quality) {
    if (maximum_width == 0 || maximum_height == 0) return nullopt;
    ComApartment apartment;
    if (!apartment.available()) return nullopt;

    ComPtr<IWICImagingFactory> factory;
    if (!succeeded(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return nullopt;
    ComPtr<IWICBitmapDecoder> decoder;
    if (!succeeded(factory->CreateDecoderFromFilename(source.c_str(), nullptr, GENERIC_READ,
                                                       WICDecodeMetadataCacheOnDemand, &decoder))) return nullopt;
    ComPtr<IWICBitmapFrameDecode> decoded;
    if (!succeeded(decoder->GetFrame(0, &decoded))) return nullopt;

    UINT source_width{}, source_height{};
    if (!succeeded(decoded->GetSize(&source_width, &source_height)) ||
        source_width == 0 || source_height == 0) return nullopt;
    const double scale = (min)(1.0, (min)(static_cast<double>(maximum_width) / source_width,
                                         static_cast<double>(maximum_height) / source_height));
    const UINT width = (max)(1U, static_cast<UINT>(lround(source_width * scale)));
    const UINT height = (max)(1U, static_cast<UINT>(lround(source_height * scale)));

    ComPtr<IWICBitmapSource> bitmap_source;
    if (width != source_width || height != source_height) {
        ComPtr<IWICBitmapScaler> scaler;
        if (!succeeded(factory->CreateBitmapScaler(&scaler)) ||
            !succeeded(scaler->Initialize(decoded.Get(), width, height,
                                          WICBitmapInterpolationModeFant))) return nullopt;
        bitmap_source = scaler;
    } else bitmap_source = decoded;

    ComPtr<IWICFormatConverter> converter;
    if (!succeeded(factory->CreateFormatConverter(&converter)) ||
        !succeeded(converter->Initialize(bitmap_source.Get(), GUID_WICPixelFormat24bppBGR,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom))) return nullopt;

    ComPtr<IStream> stream;
    if (!succeeded(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return nullopt;
    ComPtr<IWICBitmapEncoder> encoder;
    if (!succeeded(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) ||
        !succeeded(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return nullopt;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (!succeeded(encoder->CreateNewFrame(&frame, &properties))) return nullopt;
    if (properties) {
        PROPBAG2 option{};
        option.pstrName = const_cast<wchar_t*>(L"ImageQuality");
        VARIANT value{};
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = clamp(quality, 0.1F, 1.0F);
        static_cast<void>(properties->Write(1, &option, &value));
        VariantClear(&value);
    }
    if (!succeeded(frame->Initialize(properties.Get())) ||
        !succeeded(frame->SetSize(width, height))) return nullopt;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (!succeeded(frame->SetPixelFormat(&format)) ||
        !succeeded(frame->WriteSource(converter.Get(), nullptr)) ||
        !succeeded(frame->Commit()) || !succeeded(encoder->Commit())) return nullopt;

    STATSTG statistics{};
    if (!succeeded(stream->Stat(&statistics, STATFLAG_NONAME)) || statistics.cbSize.HighPart != 0)
        return nullopt;
    HGLOBAL memory{};
    if (!succeeded(GetHGlobalFromStream(stream.Get(), &memory))) return nullopt;
    const void* locked = GlobalLock(memory);
    if (!locked) return nullopt;
    vector<uint8_t> bytes(statistics.cbSize.LowPart);
    memcpy(bytes.data(), locked, bytes.size());
    GlobalUnlock(memory);
    return ImageVariant{move(bytes), width, height};
}

} // namespace homecloud
