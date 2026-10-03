#include "app/device_catalog.hpp"

#ifdef _WIN32

#include <Windows.h>
#include <propsys.h>
#include <propvarutil.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace arssyut::app {

namespace {

using Microsoft::WRL::ComPtr;

class ScopedCom final {
public:
    ScopedCom() noexcept
    {
        const HRESULT result =
            CoInitializeEx(
                nullptr,
                COINIT_APARTMENTTHREADED);

        owns_ =
            result == S_OK ||
            result == S_FALSE;
    }

    ~ScopedCom()
    {
        if (owns_)
            CoUninitialize();
    }

private:
    bool owns_ = false;
};

} // namespace

std::vector<DeviceChoice>
enumerate_microphones()
{
    ScopedCom com;
    std::vector<DeviceChoice> result;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(
                &enumerator)))) {
        return result;
    }

    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(
            eCapture,
            DEVICE_STATE_ACTIVE,
            &devices))) {
        return result;
    }

    UINT count = 0;
    if (FAILED(devices->GetCount(&count)))
        return result;

    result.reserve(count);

    for (UINT index = 0;
         index < count;
         ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(
                index,
                &device))) {
            continue;
        }

        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id)) ||
            !id) {
            continue;
        }

        ComPtr<IPropertyStore> properties;
        PROPVARIANT name_value{};
        PropVariantInit(&name_value);

        std::wstring name =
            L"Microphone";

        if (SUCCEEDED(
                device->OpenPropertyStore(
                    STGM_READ,
                    &properties)) &&
            SUCCEEDED(
                properties->GetValue(
                    PKEY_Device_FriendlyName,
                    &name_value)) &&
            name_value.vt == VT_LPWSTR &&
            name_value.pwszVal) {
            name =
                name_value.pwszVal;
        }

        DeviceChoice choice;
        choice.id = id;
        choice.name = std::move(name);
        result.push_back(
            std::move(choice));

        PropVariantClear(
            &name_value);
        CoTaskMemFree(id);
    }

    return result;
}

std::vector<DeviceChoice>
enumerate_cameras()
{
    ScopedCom com;
    std::vector<DeviceChoice> result;

    if (FAILED(MFStartup(
            MF_VERSION,
            MFSTARTUP_LITE))) {
        return result;
    }

    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(
            &attributes,
            1))) {
        MFShutdown();
        return result;
    }

    if (FAILED(attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
        MFShutdown();
        return result;
    }

    IMFActivate **devices = nullptr;
    UINT32 count = 0;

    if (FAILED(MFEnumDeviceSources(
            attributes.Get(),
            &devices,
            &count))) {
        MFShutdown();
        return result;
    }

    result.reserve(count);

    for (UINT32 index = 0;
         index < count;
         ++index) {
        IMFActivate *device =
            devices[index];
        if (!device)
            continue;

        WCHAR *friendly = nullptr;
        UINT32 friendly_length = 0;
        std::wstring name =
            L"Camera";

        if (SUCCEEDED(
                device->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                    &friendly,
                    &friendly_length)) &&
            friendly) {
            name.assign(
                friendly,
                friendly_length);
            CoTaskMemFree(friendly);
        }

        WCHAR *symbolic = nullptr;
        UINT32 symbolic_length = 0;
        std::wstring id;

        if (SUCCEEDED(
                device->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &symbolic,
                    &symbolic_length)) &&
            symbolic) {
            id.assign(
                symbolic,
                symbolic_length);
            CoTaskMemFree(symbolic);
        }

        DeviceChoice choice;
        choice.id = std::move(id);
        choice.name = std::move(name);
        result.push_back(
            std::move(choice));

        device->Release();
    }

    CoTaskMemFree(devices);
    MFShutdown();

    return result;
}

} // namespace arssyut::app

#endif
