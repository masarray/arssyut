#include "core/video/output_overlay_profile.hpp"
#include <cmath>
#include <iostream>
using namespace arssyut::core::video;
int main() {
    const auto fail=[](const char *why) { std::cerr<<why<<"\n";return 1; };
    OutputOverlayProfile defaults{};
    if(!defaults.valid() || defaults.camera.placement!=OverlayCorner::TopRight ||
       defaults.camera.mask!=CameraMask::RoundedRectangle)
        return fail("F3 default must match actual TopRight MP4");
    for(int placement=0; placement<4; ++placement) {
        for(int mask=0; mask<4; ++mask) {
            OutputOverlayProfile p{};
            p.camera.placement=static_cast<OverlayCorner>(placement);
            p.camera.mask=static_cast<CameraMask>(mask);
            if(!p.valid()) return fail("F3 4x4 placement/mask rejected");
            for(auto [w,h]:{std::pair{1920,1080}, {1280,720},
                              {3840,2160},{1080,1920}}) {
                const float f=p.camera.width_fraction;
                const float y=mask==0 ? f*float(w)/h :
                              f*float(w)/h*(3.0f/4.0f);
                const auto rect=place_output_overlay(
                    p.camera.placement,f,y,p.camera.margin_fraction);
                if(!(rect.left>=0 && rect.top>=0 && rect.right<=1 &&
                     rect.bottom<=1 && rect.right>rect.left &&
                     rect.bottom>rect.top))
                    return fail("F3 output bounds under 720p/1080p/4K/portrait");
                if(mask==0 &&
                    std::abs((rect.right-rect.left)*w-
                             (rect.bottom-rect.top)*h)>0.1f)
                    return fail("Circle became ellipse in output pixels");
            }
        }
    }
    LogoOverlayProfile logo{};
    for(int placement=0;placement<5;++placement) {
        logo.placement=static_cast<OverlayCorner>(placement);
        logo.enabled=false;
        if(!logo.valid()) return fail("Logo position unavailable when Camera OFF");
    }
    logo.enabled=true;
    if(logo.valid()) return fail("Logo enabled with no PNG must be rejected");
    logo.path=L"C:\\test\\badge.png";
    if(!logo.valid()) return fail("Bounded logo path rejected");
    defaults.camera.width_fraction=std::nanf("");
    if(defaults.valid()) return fail("NaN camera width escaped validation");
    defaults.camera.width_fraction=0.24f;
    defaults.camera.margin_fraction=-0.1f;
    if(defaults.valid()) return fail("Negative output margin escaped");
    defaults.camera.margin_fraction=0.03f;
    defaults.camera.mask=static_cast<CameraMask>(99);
    if(defaults.valid()) return fail("Unknown image mask accepted");
    std::cout<<"F3/F4 output overlay geometry/profile contract PASS\n";
    return 0;
}
