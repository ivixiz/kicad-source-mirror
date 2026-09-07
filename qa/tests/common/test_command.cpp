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

#include <boost/test/unit_test.hpp>
#include <command/command.h>

using namespace KICAD_COMMAND;

namespace
{
class TEST_CONTEXT : public CONTEXT
{
public:
    EDITOR Editor() const override { return EDITOR::PCB; }
    nlohmann::json Describe() const override { return { { "document", document } }; }
    RESULT CheckReady( EFFECT ) const override { ++checks; return ready; }

    mutable int checks = 0;
    std::string document = "first";
    RESULT ready = RESULT::Ok( "ready" );
};

DESCRIPTOR testCommand( const wxString& aName, int& aCalls )
{
    DESCRIPTOR command;
    command.name = aName;
    command.usage = aName + " value";
    command.editor = EDITOR::PCB;
    command.minArgs = 1;
    command.maxArgs = 1;
    command.handler = [&aCalls]( CONTEXT&, const std::vector<wxString>& aArgs )
    {
        ++aCalls;
        return RESULT::Ok( aArgs[0] );
    };
    return command;
}
}

BOOST_AUTO_TEST_SUITE( SemanticCommands )

BOOST_AUTO_TEST_CASE( QuotesEscapesAndEmptyArguments )
{
    const auto parsed = PARSER::Parse( R"(  pcb.get "R 1" 'a"b' "" net\ name \"q\" )" );
    BOOST_REQUIRE( parsed.result.IsOk() );
    BOOST_CHECK_EQUAL( parsed.command.name, "pcb.get" );
    BOOST_REQUIRE_EQUAL( parsed.command.args.size(), 5 );
    BOOST_CHECK_EQUAL( parsed.command.args[0], "R 1" );
    BOOST_CHECK_EQUAL( parsed.command.args[1], "a\"b" );
    BOOST_CHECK_EQUAL( parsed.command.args[2], "" );
    BOOST_CHECK_EQUAL( parsed.command.args[3], "net name" );
    BOOST_CHECK_EQUAL( parsed.command.args[4], "\"q\"" );
}

BOOST_AUTO_TEST_CASE( RejectMalformedAndMultipleLines )
{
    for( const wxString& text : { "", " \t", "pcb.get \"x", "pcb.get x\\", "context\nhelp",
                                  "context\rhelp", "PCB.get R1", "help;context" } )
    {
        BOOST_CHECK( PARSER::Parse( text ).result.status == STATUS::PARSE_ERROR );
    }

    BOOST_CHECK( PARSER::Parse( wxString( 'a', 16385 ) ).result.status == STATUS::PARSE_ERROR );
    wxString tooMany = "help";

    for( int i = 0; i < 129; ++i )
        tooMany += " a";

    BOOST_CHECK( PARSER::Parse( tooMany ).result.status == STATUS::PARSE_ERROR );
    tooMany.RemoveLast( 2 );
    BOOST_CHECK( PARSER::Parse( tooMany + " " ).result.IsOk() );
    BOOST_CHECK( PARSER::Parse( "pcb.get $(ignored)" ).result.IsOk() );
}

BOOST_AUTO_TEST_CASE( RegistryRejectsProgrammingErrors )
{
    REGISTRY registry;
    int calls = 0;
    registry.Register( testCommand( "pcb.test", calls ) );
    BOOST_CHECK_THROW( registry.Register( testCommand( "pcb.test", calls ) ), std::invalid_argument );
    BOOST_CHECK_THROW( registry.Register( testCommand( "Bad Name", calls ) ), std::invalid_argument );
    DESCRIPTOR invalid = testCommand( "pcb.invalid", calls );
    invalid.minArgs = 2;
    BOOST_CHECK_THROW( registry.Register( invalid ), std::invalid_argument );
    BOOST_CHECK( registry.Find( "pcb.test" ) );
    BOOST_CHECK( !registry.Find( "missing" ) );
}

BOOST_AUTO_TEST_CASE( ValidationPrecedesAccessAndDispatch )
{
    auto context = std::make_unique<TEST_CONTEXT>();
    TEST_CONTEXT* state = context.get();
    EXECUTOR executor( std::move( context ) );
    int calls = 0;
    executor.Registry().Register( testCommand( "pcb.test", calls ) );
    DESCRIPTOR otherEditor = testCommand( "schematic.test", calls );
    otherEditor.editor = EDITOR::SCHEMATIC;
    executor.Registry().Register( std::move( otherEditor ) );

    BOOST_CHECK( executor.Execute( "missing" ).status == STATUS::UNKNOWN_COMMAND );
    BOOST_CHECK( executor.Execute( "pcb.test" ).status == STATUS::INVALID_ARGUMENT );
    BOOST_CHECK( executor.Execute( "pcb.test a b" ).status == STATUS::INVALID_ARGUMENT );
    BOOST_CHECK( executor.Execute( "schematic.test a" ).status == STATUS::WRONG_EDITOR );
    BOOST_CHECK_EQUAL( state->checks, 0 );
    BOOST_CHECK_EQUAL( calls, 0 );

    state->ready = RESULT::Error( STATUS::BUSY, "interactive edit" );
    BOOST_CHECK( executor.Execute( "pcb.test a" ).status == STATUS::BUSY );
    BOOST_CHECK_EQUAL( calls, 0 );
    state->ready = RESULT::Error( STATUS::NO_DOCUMENT, "closed" );
    BOOST_CHECK( executor.Execute( "pcb.test a" ).status == STATUS::NO_DOCUMENT );
    BOOST_CHECK( executor.Execute( "help" ).IsOk() );
    BOOST_CHECK( executor.Execute( "help pcb.test" ).IsOk() );
    state->ready = RESULT::Ok( "ready" );
    BOOST_CHECK( executor.Execute( REQUEST{ "pcb.test", { "structured" } } ).IsOk() );
    BOOST_CHECK_EQUAL( calls, 1 );
}

BOOST_AUTO_TEST_CASE( ContextResolvesCurrentStateAndResultsAreSerializable )
{
    auto context = std::make_unique<TEST_CONTEXT>();
    TEST_CONTEXT* state = context.get();
    EXECUTOR executor( std::move( context ) );
    BOOST_CHECK_EQUAL( executor.Execute( "context" ).data["document"].get<std::string>(), "first" );
    state->document = "second";
    BOOST_CHECK_EQUAL( executor.Execute( "context" ).data["document"].get<std::string>(), "second" );
    auto result = RESULT::Ok( wxString::FromUTF8( "Ω" ), { { "x", 12.5 } }, true );
    auto json = nlohmann::json::parse( result.ToJson().dump() );
    BOOST_CHECK_EQUAL( json["status"].get<std::string>(), "ok" );
    BOOST_CHECK_EQUAL( json["message"].get<std::string>(), "Ω" );
    BOOST_CHECK( json["changed"].get<bool>() );
}

BOOST_AUTO_TEST_CASE( ReentrancyAndExceptionRecovery )
{
    EXECUTOR executor( std::make_unique<TEST_CONTEXT>() );
    DESCRIPTOR nested;
    nested.name = "nested";
    nested.handler = [&executor]( CONTEXT&, const std::vector<wxString>& )
    {
        return executor.Execute( "context" );
    };
    executor.Registry().Register( nested );
    BOOST_CHECK( executor.Execute( "nested" ).status == STATUS::BUSY );
    nested.name = "throws";
    nested.handler = []( CONTEXT&, const std::vector<wxString>& ) -> RESULT
    {
        throw std::runtime_error( "test failure" );
    };
    executor.Registry().Register( nested );
    BOOST_CHECK( executor.Execute( "throws" ).status == STATUS::FAILED );
    BOOST_CHECK( executor.Execute( "context" ).IsOk() );
}

BOOST_AUTO_TEST_CASE( BoundedHistoryRestoresDraftAndSuppressesDuplicates )
{
    HISTORY history( 2 );
    BOOST_CHECK_EQUAL( history.Previous( "draft" ), "draft" );
    history.Add( "discarded" );
    history.Add( "context" );
    history.Add( "help" );
    history.Add( "help" );
    BOOST_CHECK_EQUAL( history.Previous( "unfinished command" ), "help" );
    BOOST_CHECK_EQUAL( history.Previous( "ignored" ), "context" );
    BOOST_CHECK_EQUAL( history.Previous( "ignored" ), "context" );
    BOOST_CHECK_EQUAL( history.Next(), "help" );
    BOOST_CHECK_EQUAL( history.Next(), "unfinished command" );
    BOOST_CHECK_EQUAL( history.Next(), "unfinished command" );
    history.Add( "pcb.list_nets" );
    BOOST_CHECK_EQUAL( history.Previous( "new draft" ), "pcb.list_nets" );

    HISTORY disabled( 0 );
    disabled.Add( "help" );
    BOOST_CHECK_EQUAL( disabled.Previous( "draft" ), "draft" );
}

BOOST_AUTO_TEST_SUITE_END()
