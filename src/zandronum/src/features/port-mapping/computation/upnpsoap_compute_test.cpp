// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/port-mapping/computation/upnpsoap_compute.h"

using zx::BuildAddPortMappingBody;
using zx::BuildDeletePortMappingBody;
using zx::BuildSoapRequest;
using zx::ControlUrlForService;
using zx::HttpStatusOf;
using zx::kServiceWanIp;
using zx::kServiceWanPpp;
using zx::MapResult;
using zx::PortMapRequest;
using zx::ReadMapResponse;
using zx::UpnpErrorCode;
using zx::XmlEscape;
using zx::XmlTagValue;
using std::string;

namespace
{
// A gateway description with several services, which is what real ones look like. The service we
// want is deliberately NOT first.
const char *const kDeviceXml =
	"<root><device>"
	"<serviceList>"
	"<service>"
	"<serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
	"<controlURL>/ctl/L3F</controlURL>"
	"</service>"
	"<service>"
	"<serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType>"
	"<controlURL>/ctl/CommonIfCfg</controlURL>"
	"</service>"
	"<service>"
	"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
	"<controlURL>/ctl/IPConn</controlURL>"
	"</service>"
	"</serviceList>"
	"</device></root>";

PortMapRequest Basic()
{
	PortMapRequest request;
	request.serviceType = kServiceWanIp;
	request.internalHost = "192.168.1.50";
	request.description = "ZandroX";
	request.externalPort = 10666;
	request.internalPort = 10666;
	request.tcp = false;
	request.leaseSeconds = 3600;
	return request;
}
} // namespace

// ---------------------------------------------------------------- escaping

TEST( XmlEscaping, EscapesTheFiveThatEndAnElementEarly )
{
	EXPECT_EQ( "&amp;", XmlEscape( "&" ));
	EXPECT_EQ( "&lt;", XmlEscape( "<" ));
	EXPECT_EQ( "&gt;", XmlEscape( ">" ));
	EXPECT_EQ( "&quot;", XmlEscape( "\"" ));
	EXPECT_EQ( "&apos;", XmlEscape( "'" ));
}

TEST( XmlEscaping, LeavesOrdinaryTextAlone )
{
	// [rc4l] Ordinary letters survive, and the one character in a plausible server name that does
	// need escaping is escaped in place rather than taking the rest of the name with it.
	EXPECT_EQ( "Bob&apos;s Server", XmlEscape( "Bob's Server" ));
	EXPECT_EQ( "ZandroX 1", XmlEscape( "ZandroX 1" ));
}

TEST( XmlEscaping, ASeverNameCannotCloseTheElementItIsIn )
{
	// [rc4l] The reason this exists. The description carries a name a player typed, and a server
	// called `</NewPortMappingDescription><NewEnabled>0` would otherwise be a document the router
	// either rejects or -- worse -- reads as something we did not send.
	const string escaped = XmlEscape( "</NewPortMappingDescription><NewEnabled>0" );

	EXPECT_EQ( string::npos, escaped.find( "<" ));
	EXPECT_EQ( string::npos, escaped.find( ">" ));
}

TEST( XmlEscaping, DropsControlCharactersRatherThanEscapingThem )
{
	// Nothing legible needs them, and a router's own web UI is where this ends up displayed.
	EXPECT_EQ( "ab", XmlEscape( "a\x01\x02" "b" ));
	EXPECT_EQ( "ab", XmlEscape( "a\nb" ));
}

// ---------------------------------------------------------------- reading the description

TEST( DeviceXml, TakesTheControlUrlFromTheRightService )
{
	// [rc4l] The bug this avoids works on the developer's router and nowhere else: a gateway lists
	// several services, each with its own controlURL, and taking the first one in the document
	// addresses whichever happened to be listed first.
	EXPECT_EQ( "/ctl/IPConn", ControlUrlForService( kDeviceXml, kServiceWanIp ));
}

TEST( DeviceXml, AnAbsentServiceIsEmptyNotTheWrongOne )
{
	EXPECT_EQ( "", ControlUrlForService( kDeviceXml, kServiceWanPpp ));
	EXPECT_EQ( "", ControlUrlForService( "", kServiceWanIp ));
	EXPECT_EQ( "", ControlUrlForService( "<root></root>", kServiceWanIp ));
}

TEST( DeviceXml, SurvivesAServiceBlockThatIsNeverClosed )
{
	// Truncated documents happen; a scan that ran off the end would be worse than one that fails.
	const string truncated =
		"<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
		"<controlURL>/ctl/IPConn</controlURL>";

	EXPECT_EQ( "/ctl/IPConn", ControlUrlForService( truncated, kServiceWanIp ));
}

TEST( DeviceXml, ReadsASingleTag )
{
	EXPECT_EQ( "value", XmlTagValue( "<a><tag>value</tag></a>", "tag" ));
	EXPECT_EQ( "", XmlTagValue( "<a></a>", "tag" ));
	EXPECT_EQ( "", XmlTagValue( "<tag>unclosed", "tag" ));
	EXPECT_EQ( "", XmlTagValue( "", "tag" ));
}

// ---------------------------------------------------------------- the requests

TEST( AddMapping, CarriesEverythingTheRouterNeeds )
{
	const string body = BuildAddPortMappingBody( Basic( ));

	EXPECT_NE( string::npos, body.find( "<NewExternalPort>10666</NewExternalPort>" ));
	EXPECT_NE( string::npos, body.find( "<NewInternalPort>10666</NewInternalPort>" ));
	EXPECT_NE( string::npos, body.find( "<NewInternalClient>192.168.1.50</NewInternalClient>" ));
	EXPECT_NE( string::npos, body.find( "<NewEnabled>1</NewEnabled>" ));
	EXPECT_NE( string::npos, body.find( "<NewLeaseDuration>3600</NewLeaseDuration>" ));

	// Empty remote host means "from anywhere", which is what a public game server wants.
	EXPECT_NE( string::npos, body.find( "<NewRemoteHost></NewRemoteHost>" ));
}

TEST( AddMapping, SaysWhichProtocol )
{
	PortMapRequest udp = Basic( );
	udp.tcp = false;
	EXPECT_NE( string::npos, BuildAddPortMappingBody( udp ).find( "<NewProtocol>UDP</NewProtocol>" ));

	PortMapRequest tcp = Basic( );
	tcp.tcp = true;
	EXPECT_NE( string::npos, BuildAddPortMappingBody( tcp ).find( "<NewProtocol>TCP</NewProtocol>" ));
}

TEST( AddMapping, EscapesTheDescription )
{
	PortMapRequest request = Basic( );
	request.description = "Bob & <friends>";

	const string body = BuildAddPortMappingBody( request );

	EXPECT_NE( string::npos, body.find( "Bob &amp; &lt;friends&gt;" ));
}

TEST( AddMapping, TrimsADescriptionNobodyCouldRead )
{
	PortMapRequest request = Basic( );
	request.description = string( 400, 'x' );

	// The field ends up in a router's web UI; some firmware simply rejects an over-long one.
	EXPECT_LT( BuildAddPortMappingBody( request ).size( ), static_cast<size_t>( 900 ));
}

TEST( DeleteMapping, IdentifiesTheMappingByPortAndProtocolOnly )
{
	const string body = BuildDeletePortMappingBody( kServiceWanIp, 10666, true );

	EXPECT_NE( string::npos, body.find( "DeletePortMapping" ));
	EXPECT_NE( string::npos, body.find( "<NewExternalPort>10666</NewExternalPort>" ));
	EXPECT_NE( string::npos, body.find( "<NewProtocol>TCP</NewProtocol>" ));
	EXPECT_NE( string::npos, BuildDeletePortMappingBody( kServiceWanIp, 1, false )
		.find( "<NewProtocol>UDP</NewProtocol>" ));
}

TEST( SoapRequest, CarriesTheActionAndAMatchingLength )
{
	const string body = "<x/>";
	const string request = BuildSoapRequest( "192.168.1.1", 5000, "/ctl/IPConn", kServiceWanIp,
		"AddPortMapping", body );

	EXPECT_EQ( 0u, request.find( "POST /ctl/IPConn HTTP/1.1\r\n" ));
	EXPECT_NE( string::npos, request.find( "HOST: 192.168.1.1:5000\r\n" ));

	// [rc4l] Not optional: a gateway that cannot see which action is meant answers 500 to
	// everything, and the resulting "your router refused" is a lie about a request we malformed.
	EXPECT_NE( string::npos,
		request.find( string( "SOAPACTION: \"" ) + kServiceWanIp + "#AddPortMapping\"" ));

	EXPECT_NE( string::npos, request.find( "CONTENT-LENGTH: 4\r\n" ));
	EXPECT_NE( string::npos, request.find( "\r\n\r\n<x/>" ));
}

TEST( SoapRequest, OmitsThePortFromTheHostHeaderWhenItIsTheDefault )
{
	const string request = BuildSoapRequest( "10.0.0.1", 80, "/ctl", kServiceWanIp, "A", "" );

	EXPECT_NE( string::npos, request.find( "HOST: 10.0.0.1\r\n" ));
}

// ---------------------------------------------------------------- the answers

TEST( MapResponse, TwoHundredIsSuccess )
{
	EXPECT_EQ( MapResult::Ok, ReadMapResponse( "HTTP/1.1 200 OK\r\n\r\n" ));
}

TEST( MapResponse, ADuplicateMappingIsItsOwnAnswer )
{
	// [rc4l] Worth distinguishing, because it is the one failure a player can act on -- somebody
	// else already has that port, so try another. "Your router refused" would send them to the
	// wrong place entirely.
	const string conflict =
		"HTTP/1.1 500 Internal Server Error\r\n\r\n"
		"<s:Envelope><s:Body><s:Fault><detail><UPnPError>"
		"<errorCode>718</errorCode></UPnPError></detail></s:Fault></s:Body></s:Envelope>";

	EXPECT_EQ( MapResult::Conflict, ReadMapResponse( conflict ));
	EXPECT_EQ( 718, UpnpErrorCode( conflict ));
}

TEST( MapResponse, ARefusalIsNotAMalfunction )
{
	// UPnP switched off on the router, which is a legitimate configuration and increasingly a
	// default -- not something to report as an error.
	const string refused =
		"HTTP/1.1 500 Internal Server Error\r\n\r\n"
		"<UPnPError><errorCode>606</errorCode></UPnPError>";

	EXPECT_EQ( MapResult::Refused, ReadMapResponse( refused ));
}

TEST( MapResponse, AnythingUnrecognisedIsSimplyAFailure )
{
	EXPECT_EQ( MapResult::Failed, ReadMapResponse( "HTTP/1.1 500 Error\r\n\r\n" ));
	EXPECT_EQ( MapResult::Failed, ReadMapResponse( "HTTP/1.1 404 Not Found\r\n\r\n" ));
	EXPECT_EQ( MapResult::Failed, ReadMapResponse( "" ));
	EXPECT_EQ( MapResult::Failed, ReadMapResponse( "garbage" ));

	const string unknown =
		"HTTP/1.1 500 Error\r\n\r\n<UPnPError><errorCode>402</errorCode></UPnPError>";
	EXPECT_EQ( MapResult::Failed, ReadMapResponse( unknown ));
}

TEST( MapResponse, RecognisesTheOtherCodesWorthTellingApart )
{
	const string samePort =
		"HTTP/1.1 500 E\r\n\r\n<UPnPError><errorCode>724</errorCode></UPnPError>";
	EXPECT_EQ( MapResult::Conflict, ReadMapResponse( samePort ));

	const string noEntry =
		"HTTP/1.1 500 E\r\n\r\n<UPnPError><errorCode>714</errorCode></UPnPError>";
	EXPECT_EQ( MapResult::Refused, ReadMapResponse( noEntry ));

	const string permanentOnly =
		"HTTP/1.1 500 E\r\n\r\n<UPnPError><errorCode>725</errorCode></UPnPError>";
	EXPECT_EQ( MapResult::Refused, ReadMapResponse( permanentOnly ));
}

TEST( HttpStatus, ReadsTheCodeOrSaysItCannot )
{
	EXPECT_EQ( 200, HttpStatusOf( "HTTP/1.1 200 OK\r\n" ));
	EXPECT_EQ( 500, HttpStatusOf( "HTTP/1.0 500 Internal Server Error\r\n" ));

	EXPECT_EQ( 0, HttpStatusOf( "" ));
	EXPECT_EQ( 0, HttpStatusOf( "not http at all" ));
	EXPECT_EQ( 0, HttpStatusOf( "HTTP/1.1" ));
}

TEST( UpnpError, IsZeroWhenThereIsNoFault )
{
	EXPECT_EQ( 0, UpnpErrorCode( "HTTP/1.1 200 OK\r\n\r\n" ));
	EXPECT_EQ( 0, UpnpErrorCode( "" ));
}
