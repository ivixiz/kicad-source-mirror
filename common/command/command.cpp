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

#include <command/command.h>

#include <stdexcept>
#include <utility>

namespace KICAD_COMMAND
{
namespace
{
constexpr size_t MAX_COMMAND_LENGTH = 16384;
constexpr size_t MAX_ARGUMENTS = 128;

bool validName( const wxString& aName )
{
    if( aName.empty() || aName[0] < 'a' || aName[0] > 'z' )
        return false;

    for( wxUniChar c : aName )
    {
        if( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' )
               || c == '.' || c == '_' ) )
        {
            return false;
        }
    }

    return true;
}

nlohmann::json describeCommand( const DESCRIPTOR& aCommand )
{
    const char* effect = aCommand.effect == EFFECT::EDIT ? "edit"
                        : aCommand.effect == EFFECT::CHECK ? "check" : "query";

    return { { "name", aCommand.name.ToStdString( wxConvUTF8 ) },
             { "usage", aCommand.usage.ToStdString( wxConvUTF8 ) },
             { "description", aCommand.description.ToStdString( wxConvUTF8 ) },
             { "editor", aCommand.editor ? EditorName( *aCommand.editor ) : "any" },
             { "effect", effect }, { "min_args", aCommand.minArgs },
             { "max_args", aCommand.maxArgs } };
}
} // namespace

const char* StatusName( STATUS aStatus )
{
    switch( aStatus )
    {
    case STATUS::OK:               return "ok";
    case STATUS::PARSE_ERROR:      return "parse_error";
    case STATUS::UNKNOWN_COMMAND:  return "unknown_command";
    case STATUS::INVALID_ARGUMENT: return "invalid_argument";
    case STATUS::WRONG_EDITOR:     return "wrong_editor";
    case STATUS::NO_DOCUMENT:      return "no_document";
    case STATUS::BUSY:             return "busy";
    case STATUS::NOT_FOUND:        return "not_found";
    case STATUS::AMBIGUOUS:        return "ambiguous";
    case STATUS::LOCKED:           return "locked";
    case STATUS::CANCELLED:        return "cancelled";
    case STATUS::FAILED:           return "failed";
    }

    return "failed";
}

const char* EditorName( EDITOR aEditor )
{
    return aEditor == EDITOR::PCB ? "pcb" : "schematic";
}

nlohmann::json RESULT::ToJson() const
{
    return { { "status", StatusName( status ) }, { "message", message.ToStdString( wxConvUTF8 ) },
             { "data", data }, { "changed", changed } };
}

RESULT RESULT::Ok( wxString aMessage, nlohmann::json aData, bool aChanged )
{
    return { STATUS::OK, std::move( aMessage ), std::move( aData ), aChanged };
}

RESULT RESULT::Error( STATUS aStatus, wxString aMessage )
{
    return { aStatus, std::move( aMessage ), nlohmann::json::object(), false };
}

PARSE_RESULT PARSER::Parse( const wxString& aText )
{
    auto error = []( const wxString& aMessage ) -> PARSE_RESULT
    {
        return { RESULT::Error( STATUS::PARSE_ERROR, aMessage ), {} };
    };

    if( aText.length() > MAX_COMMAND_LENGTH )
        return error( "Command exceeds 16384 characters." );

    std::vector<wxString> tokens;
    wxString token;
    wxUniChar quote = 0;
    bool escaped = false;
    bool started = false;

    for( wxUniChar c : aText )
    {
        if( c == '\n' || c == '\r' || c == 0 )
            return error( "Enter one command per line." );

        if( escaped )
        {
            token += c;
            escaped = false;
        }
        else if( c == '\\' )
        {
            escaped = true;
            started = true;
        }
        else if( quote != 0 )
        {
            if( c == quote )
                quote = 0;
            else
                token += c;
        }
        else if( c == '\'' || c == '"' )
        {
            quote = c;
            started = true;
        }
        else if( c == ' ' || c == '\t' )
        {
            if( started )
            {
                tokens.push_back( token );
                token.clear();
                started = false;
            }
        }
        else
        {
            token += c;
            started = true;
        }

        if( tokens.size() > MAX_ARGUMENTS + 1 )
            return error( "Too many command arguments." );
    }

    if( escaped || quote != 0 )
        return error( escaped ? "Incomplete backslash escape." : "Unterminated quoted argument." );

    if( started )
        tokens.push_back( token );

    if( tokens.empty() )
        return error( "Enter a command. Type help to list commands." );

    if( tokens.size() > MAX_ARGUMENTS + 1 )
        return error( "Too many command arguments." );

    if( !validName( tokens.front() ) )
        return error( "Command names use lowercase letters, digits, dots and underscores." );

    REQUEST request;
    request.name = tokens.front();
    request.args.assign( tokens.begin() + 1, tokens.end() );
    return { RESULT::Ok( wxEmptyString ), std::move( request ) };
}

void REGISTRY::Register( DESCRIPTOR aCommand )
{
    if( !validName( aCommand.name ) || !aCommand.handler || aCommand.minArgs > aCommand.maxArgs )
        throw std::invalid_argument( "Invalid command descriptor" );

    wxString name = aCommand.name;

    if( !m_commands.emplace( name, std::move( aCommand ) ).second )
        throw std::invalid_argument( "Duplicate command registration" );
}

const DESCRIPTOR* REGISTRY::Find( const wxString& aName ) const
{
    auto it = m_commands.find( aName );
    return it == m_commands.end() ? nullptr : &it->second;
}

EXECUTOR::EXECUTOR( std::unique_ptr<CONTEXT> aContext ) : m_context( std::move( aContext ) )
{
    if( !m_context )
        throw std::invalid_argument( "Command executor requires an editor context" );

    DESCRIPTOR help;
    help.name = "help";
    help.usage = "help [command]";
    help.description = "List available commands or describe one command.";
    help.maxArgs = 1;
    help.requiresDocument = false;
    help.handler = [this]( CONTEXT&, const std::vector<wxString>& aArgs )
    {
        if( !aArgs.empty() )
        {
            const DESCRIPTOR* command = m_registry.Find( aArgs[0] );

            if( !command )
                return RESULT::Error( STATUS::UNKNOWN_COMMAND, "Unknown command: " + aArgs[0] );

            return RESULT::Ok( command->usage + " — " + command->description,
                               describeCommand( *command ) );
        }

        nlohmann::json commands = nlohmann::json::array();
        wxString message;

        for( const auto& [name, command] : m_registry.Commands() )
        {
            if( command.editor && *command.editor != m_context->Editor() )
                continue;

            if( !message.empty() )
                message += "\n";

            message += command.usage + " — " + command.description;
            commands.push_back( describeCommand( command ) );
        }

        return RESULT::Ok( message, { { "commands", commands } } );
    };
    m_registry.Register( std::move( help ) );

    DESCRIPTOR context;
    context.name = "context";
    context.usage = "context";
    context.description = "Describe the active editor, live document and selection.";
    context.requiresDocument = false;
    context.handler = []( CONTEXT& aContext, const std::vector<wxString>& )
    {
        return RESULT::Ok( "Current editor context", aContext.Describe() );
    };
    m_registry.Register( std::move( context ) );
}

RESULT EXECUTOR::Execute( const wxString& aText )
{
    PARSE_RESULT parsed = PARSER::Parse( aText );
    return parsed.result.IsOk() ? Execute( parsed.command ) : parsed.result;
}

RESULT EXECUTOR::Execute( const REQUEST& aRequest )
{
    if( m_executing )
        return RESULT::Error( STATUS::BUSY, "A command is already running in this editor." );

    const DESCRIPTOR* command = m_registry.Find( aRequest.name );

    if( !command )
        return RESULT::Error( STATUS::UNKNOWN_COMMAND, "Unknown command: " + aRequest.name
                              + ". Type help to list commands for this editor." );

    if( command->editor && *command->editor != m_context->Editor() )
        return RESULT::Error( STATUS::WRONG_EDITOR, "This command requires the "
                              + wxString::FromUTF8( EditorName( *command->editor ) ) + " editor." );

    if( aRequest.args.size() < command->minArgs || aRequest.args.size() > command->maxArgs )
        return RESULT::Error( STATUS::INVALID_ARGUMENT, "Usage: " + command->usage );

    // Some checks yield to the event loop; prevent nested dispatch until the handler returns.
    struct EXECUTION_GUARD
    {
        explicit EXECUTION_GUARD( bool& aExecuting ) : executing( aExecuting ) { executing = true; }
        ~EXECUTION_GUARD() { executing = false; }
        bool& executing;
    } guard( m_executing );

    try
    {
        if( command->requiresDocument )
        {
            RESULT ready = m_context->CheckReady( command->effect );

            if( !ready.IsOk() )
                return ready;
        }

        return command->handler( *m_context, aRequest.args );
    }
    catch( const std::exception& error )
    {
        // Editing handlers must revert their COMMIT before propagating an exception.
        return RESULT::Error( STATUS::FAILED, wxString::FromUTF8( error.what() ) );
    }
}

void HISTORY::Add( const wxString& aCommand )
{
    if( m_limit && !aCommand.IsEmpty()
        && ( m_entries.empty() || m_entries.back() != aCommand ) )
    {
        if( m_entries.size() == m_limit )
            m_entries.erase( m_entries.begin() );

        m_entries.push_back( aCommand );
    }

    ResetNavigation();
}

wxString HISTORY::Previous( const wxString& aDraft )
{
    if( m_entries.empty() )
        return aDraft;

    if( m_position == m_entries.size() )
        m_draft = aDraft;

    if( m_position > 0 )
        --m_position;

    return m_entries[m_position];
}

wxString HISTORY::Next()
{
    if( m_position < m_entries.size() )
        ++m_position;

    return m_position == m_entries.size() ? m_draft : m_entries[m_position];
}

void HISTORY::ResetNavigation()
{
    m_position = m_entries.size();
    m_draft.clear();
}
} // namespace KICAD_COMMAND
