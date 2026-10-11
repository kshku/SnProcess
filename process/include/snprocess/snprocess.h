#pragma once

/**
 * @file snprocess.h
 * @brief Umbrella header for the SnProcess library.
 *
 * Includes all public API headers of the process and pipe abstraction library.
 * Include this single header to gain access to all types and functions, or
 * include individual headers (pipe.h, process.h, command.h) for finer
 * granularity.
 */

#include "snprocess/api.h"
#include "snprocess/command.h"
#include "snprocess/pipe.h"
#include "snprocess/process.h"

#include <sncore/defines.h>
