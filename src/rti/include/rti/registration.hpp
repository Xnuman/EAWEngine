#pragma once
// A block of type registrations that runs when the program starts (Singe's RTI_REGISTRATION), at
// most one per source file:
//
//     EAW_RTI_REGISTRATION
//     {
//         eaw::rti::register_type<Health>().property("points", &Health::points);
//     }
//
// The block is a static object's constructor, so the library that holds it must be linked whole:
// nothing references the object, and a static library's unreferenced object files are dropped. Link
// such libraries with $<LINK_LIBRARY:WHOLE_ARCHIVE,library> (src/rti/CMakeLists.txt).
#include "rti/registrator.hpp"

#define EAW_RTI_REGISTRATION                                                                                                                                                                           \
    static void eaw_rti_register_types_();                                                                                                                                                             \
    namespace {                                                                                                                                                                                        \
    struct EawRtiRegistration                                                                                                                                                                          \
    {                                                                                                                                                                                                  \
        EawRtiRegistration()                                                                                                                                                                           \
        {                                                                                                                                                                                              \
            eaw_rti_register_types_();                                                                                                                                                                 \
        }                                                                                                                                                                                              \
    };                                                                                                                                                                                                 \
    const EawRtiRegistration eaw_rti_registration_;                                                                                                                                                    \
    }                                                                                                                                                                                                  \
    static void eaw_rti_register_types_()
