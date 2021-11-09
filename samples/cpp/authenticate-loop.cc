// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 Intel Corporation. All Rights Reserved.

#include "RealSenseID/FaceAuthenticator.h"
#include <iostream>
#include <stdio.h>

class MyAuthClbk : public RealSenseID::AuthenticationCallback
{
public:
    void OnResult(const RealSenseID::AuthenticateStatus status, const char* user_id) override
    {
        if (status == RealSenseID::AuthenticateStatus::Success)
            std::cout << "Authenticated " << user_id << std::endl;
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint) override
    {
        std::cout << "OnHint " << hint << std::endl;
    }

    void OnFaceDetected(const std::vector<RealSenseID::FaceRect>& faces, const unsigned int ts) override
    {
        for (auto& face : faces)
        {
            printf("** Detected face %u,%u %ux%u (timestamp %u)\n", face.x, face.y, face.w, face.h, ts);
        }
    }
};

int main()
{
    RealSenseID::FaceAuthenticator authenticator;
#ifdef _WIN32
    auto status = authenticator.Connect({"COM9"});
#elif LINUX
    auto status = authenticator.Connect({"/dev/ttyACM0"});
#endif
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed connecting with status " << status << std::endl;
        return 1;
    }
    MyAuthClbk auth_clbk;
    while (true) {
        authenticator.Authenticate(auth_clbk);
    }
}
