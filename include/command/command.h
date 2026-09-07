/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef KICAD_COMMAND_H
#define KICAD_COMMAND_H

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>
#include <wx/string.h>

/** Semantic commands; no window, transport, file I/O or editor-model dependencies. */
namespace KICAD_COMMAND
{
enum class EDITOR { PCB, SCHEMATIC };
enum class EFFECT { QUERY, EDIT, CHECK };
enum class STATUS
{
    OK, PARSE_ERROR, UNKNOWN_COMMAND, INVALID_ARGUMENT, WRONG_EDITOR, NO_DOCUMENT,
    BUSY, NOT_FOUND, AMBIGUOUS, LOCKED, CANCELLED, FAILED
};

const char* StatusName( STATUS aStatus );
const char* EditorName( EDITOR aEditor );

/** Data contains values and stable IDs, never pointers into an editor document. */
struct RESULT
{
    STATUS         status = STATUS::OK;
    wxString       message;
    nlohmann::json data = nlohmann::json::object();
    bool           changed = false;

    bool IsOk() const { return status == STATUS::OK; }
    nlohmann::json ToJson() const;

    static RESULT Ok( wxString aMessage, nlohmann::json aData = nlohmann::json::object(),
                      bool aChanged = false );
    static RESULT Error( STATUS aStatus, wxString aMessage );
};

struct REQUEST
{
    wxString              name;
    std::vector<wxString> args;
};

struct PARSE_RESULT
{
    RESULT  result;
    REQUEST command;
};

class PARSER
{
public:
    /// One command, whitespace-delimited arguments, single/double quotes and backslash escapes.
    /// There is no shell expansion, script evaluation or command chaining.
    static PARSE_RESULT Parse( const wxString& aText );
};

/**
 * Editor adapter owned by the executor. Implementations retain their frame, resolving the
 * current document and selection on each invocation. All calls run on the editor's UI thread.
 */
class CONTEXT
{
public:
    virtual ~CONTEXT() = default;
    virtual EDITOR Editor() const = 0;
    virtual nlohmann::json Describe() const = 0;
    virtual RESULT CheckReady( EFFECT aEffect ) const = 0;
};

struct DESCRIPTOR
{
    wxString name;
    wxString usage;
    wxString description;
    std::optional<EDITOR> editor;
    EFFECT effect = EFFECT::QUERY;
    size_t minArgs = 0;
    size_t maxArgs = 0;
    std::function<RESULT( CONTEXT&, const std::vector<wxString>& )> handler;
    bool requiresDocument = true;
};

/** Explicit registration avoids static initialization order and cross-KIFACE dependencies. */
class REGISTRY
{
public:
    /// Invalid or duplicate registrations are programming errors and throw invalid_argument.
    void Register( DESCRIPTOR aCommand );
    const DESCRIPTOR* Find( const wxString& aName ) const;
    const std::map<wxString, DESCRIPTOR>& Commands() const { return m_commands; }

private:
    std::map<wxString, DESCRIPTOR> m_commands;
};

class EXECUTOR
{
public:
    explicit EXECUTOR( std::unique_ptr<CONTEXT> aContext );

    EXECUTOR( const EXECUTOR& ) = delete;
    EXECUTOR& operator=( const EXECUTOR& ) = delete;

    REGISTRY& Registry() { return m_registry; }
    const REGISTRY& Registry() const { return m_registry; }

    RESULT Execute( const wxString& aText );
    /// Structured callers use the same validation and dispatch path as text callers.
    RESULT Execute( const REQUEST& aRequest );

private:
    REGISTRY                 m_registry;
    std::unique_ptr<CONTEXT>  m_context;
    bool                     m_executing = false;
};

/** Bounded, session-local history, including the draft saved on first Up navigation. */
class HISTORY
{
public:
    explicit HISTORY( size_t aLimit = 200 ) : m_limit( aLimit ) {}

    void Add( const wxString& aCommand );
    wxString Previous( const wxString& aDraft );
    wxString Next();
    void ResetNavigation();

private:
    std::vector<wxString> m_entries;
    size_t               m_limit;
    size_t               m_position = 0;
    wxString             m_draft;
};
} // namespace KICAD_COMMAND

#endif // KICAD_COMMAND_H
