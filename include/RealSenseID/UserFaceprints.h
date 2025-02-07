/*******************************************************************************
INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2011-2020 Intel Corporation. All Rights Reserved.
*******************************************************************************/
#pragma once

#include "Faceprints.h"
#include <string>
namespace RealSenseID
{
    struct UserFaceprints
    {
        std::string user_id;
        Faceprints faceprints;
    };
}
