#pragma once
#include "pch.h"



int GetPlayerOffset(bool derefer = false) {
    if (!derefer)
        return 0x021703D4;
    else
        return *(int*)0x021703D4;

}