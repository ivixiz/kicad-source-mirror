/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PCB_COMMANDS_H
#define PCB_COMMANDS_H

#include <memory>

class PCB_EDIT_FRAME;

namespace KICAD_COMMAND
{
class EXECUTOR;

/** The frame must outlive the executor; board pointers are resolved for every command. */
std::unique_ptr<EXECUTOR> CreatePcbCommandExecutor( PCB_EDIT_FRAME& aFrame );
}

#endif
