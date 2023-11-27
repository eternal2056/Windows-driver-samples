﻿////////////////////////////////////////////////////////////////////////////////////////////////////
//
//   Copyright (c) 2012 Microsoft Corporation.  All Rights Reserved.
//
//   Module Name:
//      ClassifyFunctions_AdvancedPacketInjectionCallouts.cpp
//
//   Abstract:
//      This module contains WFP Classify functions for injecting packets back into the data path 
//         using the allocate / block / inject method.
//
//   Naming Convention:
//
//      <Module><Scenario>
//  
//      i.e.
//       ClassifyAdvancedPacketInjection
//
//       <Module>
//          Classify                  - Function is an FWPS_CALLOUT_CLASSIFY_FN
//       <Scenario>
//          AdvancedPacketInjection   - Function demonstrates the allocate / block / inject model.
//
//      <Action><Scenario><Modifier>
//
//      i.e.
//       TriggerAdvancedPacketInjectionOutOfBand
//
//       <Action>
//        {
//                                    -
//          Trigger                   - Initiates the desired scenario.
//          Perform                   - Executes the desired scenario.
//        }
//       <Scenario>
//          AdvancedPacketInjection   - Function demonstrates the allocate / block / inject model.
//       <Modifier>
//          DeferredProcedureCall     - DPC routine for Out of Band injection which dispatches the 
//                                         proper Perform Function.
//          WorkItemRoutine           - WorkItem Routine for Out of Band Injection which dispatches
//                                         the proper Perform Function.
//          AtInboundMACFrame         - Function operates on:
//                                         FWPM_LAYER_INBOUND_MAC_FRAME_ETHERNET, and 
//                                         FWPM_LAYER_INBOUND_MAC_NATIVE.
//          AtOutboundMACFrame        - Function operates on:
//                                         FWPM_LAYER_OUTBOUND_MAC_FRAME_ETHERNET, and 
//                                         FWPM_LAYER_OUTBOUND_MAC_NATIVE.
//          AtEgressVSwitchEthernet   - Function operates on:
//                                         FWPM_LAYER_EGRESS_VSWITCH_ETHERNET.
//          AtIngressVSwitchEthernet  - Function operates on:
//                                         FWPM_LAYER_INGRESS_VSWITCH_ETHERNET.
//          AtInboundNetwork          - Function operates on:
//                                         FWPM_LAYER_INBOUND_IPPACKET_V{4/6}
//          AtOutboundNetwork         - Function operates on:
//                                         FWPM_LAYER_OUTBOUND_IPPACKET_V{4/6}
//          AtForward                 - Function operates on:
//                                         FWPM_LAYER_IPFORWARD_V{4/6}
//          AtInboundTransport        - Function operates on: 
//                                         FWPM_LAYER_INBOUND_TRANSPORT_V{4/6},
//                                         FWPM_LAYER_INBOUND_ICMP_ERROR_V{4/6},
//                                         FWPM_LAYER_DATAGRAM_DATA_V{4/6},
//                                         FWPM_LAYER_STREAM_PACKET_V{4/6}, and 
//                                         FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V{4/6}
//                                         FWPM_LAYER_ALE_FLOW_ESTABLISHED_V{4/6}
//          AtOutboundTransport       - Function operates on:
//                                         FWPM_LAYER_OUTBOUND_TRANSPORT_V{4/6},
//                                         FWPM_LAYER_OUTBOUND_ICMP_ERROR_V{4/6},
//                                         FWPM_LAYER_DATAGRAM_DATA_V{4/6},
//                                         FWPM_LAYER_STREAM_PACKET_V{4/6}, and 
//                                         FWPM_LAYER_ALE_AUTH_CONNECT_V{4/6}
//                                         FWPM_LAYER_ALE_FLOW_ESTABLISHED_V{4/6}
//
//   Private Functions:
//      AdvancedPacketInjectionDeferredProcedureCall(),
//      AdvancedPacketInjectionWorkItemRoutine(),
//      PerformAdvancedPacketInjectionAtEgressVSwitchEthernet(),
//      PerformAdvancedPacketInjectionAtForward(),
//      PerformAdvancedPacketInjectionAtInboundMACFrame(),
//      PerformAdvancedPacketInjectionAtInboundNetwork(),
//      PerformAdvancedPacketInjectionAtInboundTransport(),
//      PerformAdvancedPacketInjectionAtIngressVSwitchEthernet(),
//      PerformAdvancedPacketInjectionAtOutboundMACFrame(),
//      PerformAdvancedPacketInjectionAtOutboundNetwork(),
//      PerformAdvancedPacketInjectionAtOutboundTransport(),
//      TriggerAdvancedPacketInjectionInline(),
//      TriggerAdvancedPacketInjectionOutOfBand(),
//
//   Public Functions:
//      ClassifyAdvancedPacketInjection(),
//
//   Author:
//      Dusty Harper      (DHarper)
//
//   Revision History:
//
//      [ Month ][Day] [Year] - [Revision]-[ Comments ]
//      May       01,   2010  -     1.0   -  Creation
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Framework_WFPSamplerCalloutDriver.h"                /// .
#include "ClassifyFunctions_AdvancedPacketInjectionCallouts.tmh" /// $(OBJ_PATH)\$(O)\ 

#if DBG

INJECTION_COUNTERS g_apiTotalClassifies = { 0 };
INJECTION_COUNTERS g_apiTotalBlockedAndAbsorbed = { 0 };
INJECTION_COUNTERS g_apiTotalPermitted = { 0 };
INJECTION_COUNTERS g_apiTotalSuccessfulInjectionCalls = { 0 };
INJECTION_COUNTERS g_apiTotalFailedInjectionCalls = { 0 };
INJECTION_COUNTERS g_apiOutstandingNewNBLs = { 0 };

/**
 @function="PrvAdvancedPacketInjectionCountersIncrement"

   Purpose:  Increment the appropriate counters based on the layerId and direction.             <br>
																								<br>
   Notes:                                                                                       <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Desktop/MS683615.aspx                      <br>
*/
VOID PrvAdvancedPacketInjectionCountersIncrement(_In_ const FWPS_INCOMING_VALUES* pClassifyValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* pMetadataValues,
	_Inout_ INJECTION_COUNTERS* pCounters)
{
	UINT32     direction = FWP_DIRECTION_MAX;
	FWP_VALUE* pDirectionValue = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_DIRECTION);

	direction = KrnlHlprFwpsLayerGetDirection(pClassifyValues->layerId);

	if (pDirectionValue &&
		pDirectionValue->type == FWP_UINT32)
		direction = (FWP_DIRECTION)pDirectionValue->uint32;
	else
	{
		if (pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V4_DISCARD ||
			pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V6 ||
			pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V6_DISCARD)
		{
			if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadataValues,
				FWPS_METADATA_FIELD_FORWARD_LAYER_INBOUND_PASS_THRU))
				direction = FWP_DIRECTION_INBOUND;
			else if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadataValues,
				FWPS_METADATA_FIELD_FORWARD_LAYER_OUTBOUND_PASS_THRU))
				direction = FWP_DIRECTION_OUTBOUND;
		}
		else
		{
			if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadataValues,
				FWPS_METADATA_FIELD_PACKET_DIRECTION))
				direction = pMetadataValues->packetDirection;
		}
	}

	switch (pClassifyValues->layerId)
	{
	case FWPS_LAYER_INBOUND_IPPACKET_V4:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->inboundNetwork_IPv4));

		break;
	}
	case FWPS_LAYER_INBOUND_IPPACKET_V6:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->inboundNetwork_IPv6));

		break;
	}
	case FWPS_LAYER_OUTBOUND_IPPACKET_V4:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->outboundNetwork_IPv4));

		break;
	}
	case FWPS_LAYER_OUTBOUND_IPPACKET_V6:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->outboundNetwork_IPv6));

		break;
	}
	case FWPS_LAYER_IPFORWARD_V4:
	{
		if (direction == FWP_DIRECTION_OUTBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundForward_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundForward_IPv4));

		break;
	}
	case FWPS_LAYER_IPFORWARD_V6:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundForward_IPv6));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundForward_IPv6));

		break;
	}
	case FWPS_LAYER_INBOUND_TRANSPORT_V4:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv4));

		break;
	}
	case FWPS_LAYER_INBOUND_TRANSPORT_V6:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv6));

		break;
	}
	case FWPS_LAYER_OUTBOUND_TRANSPORT_V4:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv4));

		break;
	}
	case FWPS_LAYER_OUTBOUND_TRANSPORT_V6:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv6));


		break;
	}
	case FWPS_LAYER_DATAGRAM_DATA_V4:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv4));

		break;
	}
	case FWPS_LAYER_DATAGRAM_DATA_V6:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv6));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv6));

		break;
	}
	case FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv4));

		break;
	}
	case FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv6));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv6));

		break;
	}
	case FWPS_LAYER_ALE_AUTH_CONNECT_V4:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv4));

		break;
	}
	case FWPS_LAYER_ALE_AUTH_CONNECT_V6:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv6));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv6));

		break;
	}
	case FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv4));

		break;
	}
	case FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv6));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv6));

		break;
	}

#if(NTDDI_VERSION >= NTDDI_WIN7)

	case FWPS_LAYER_STREAM_PACKET_V4:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv4));

		break;
	}
	case FWPS_LAYER_STREAM_PACKET_V6:
	{
		if (direction == FWP_DIRECTION_INBOUND)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv6));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv6));

		break;
	}

#if(NTDDI_VERSION >= NTDDI_WIN8)

	case FWPS_LAYER_INBOUND_MAC_FRAME_ETHERNET:
	{
		UINT16 etherType = pClassifyValues->incomingValue[FWPS_FIELD_INBOUND_MAC_FRAME_ETHERNET_ETHER_TYPE].value.uint16;

		if (etherType == 0x86DD)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundMAC_IPv6));
		else if (etherType == 0x800)
			InterlockedIncrement64((LONG64*)&(pCounters->inboundMAC_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->inboundMAC_Unknown));

		break;
	}
	case FWPS_LAYER_OUTBOUND_MAC_FRAME_ETHERNET:
	{
		UINT16 etherType = pClassifyValues->incomingValue[FWPS_FIELD_OUTBOUND_MAC_FRAME_ETHERNET_ETHER_TYPE].value.uint16;

		if (etherType == 0x86DD)
			InterlockedIncrement64((LONG64*)&(pCounters->outboundMAC_IPv6));
		else if (etherType == 0x800)
			InterlockedIncrement64((LONG64*)&(pCounters->outboundMAC_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->outboundMAC_Unknown));

		break;
	}
	case FWPS_LAYER_INBOUND_MAC_FRAME_NATIVE:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->inboundMAC_Unknown));

		break;
	}
	case FWPS_LAYER_OUTBOUND_MAC_FRAME_NATIVE:
	{
		InterlockedIncrement64((LONG64*)&(pCounters->outboundMAC_Unknown));

		break;
	}
	case FWPS_LAYER_INGRESS_VSWITCH_ETHERNET:
	{
		UINT16 etherType = pClassifyValues->incomingValue[FWPS_FIELD_INGRESS_VSWITCH_ETHERNET_ETHER_TYPE].value.uint16;

		if (etherType == 0x86DD)
			InterlockedIncrement64((LONG64*)&(pCounters->ingressVSwitch_IPv6));
		else if (etherType == 0x800)
			InterlockedIncrement64((LONG64*)&(pCounters->ingressVSwitch_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->ingressVSwitch_Unknown));

		break;
	}
	case FWPS_LAYER_EGRESS_VSWITCH_ETHERNET:
	{
		UINT16 etherType = pClassifyValues->incomingValue[FWPS_FIELD_EGRESS_VSWITCH_ETHERNET_ETHER_TYPE].value.uint16;

		if (etherType == 0x86DD)
			InterlockedIncrement64((LONG64*)&(pCounters->egressVSwitch_IPv6));
		else if (etherType == 0x800)
			InterlockedIncrement64((LONG64*)&(pCounters->egressVSwitch_IPv4));
		else
			InterlockedIncrement64((LONG64*)&(pCounters->egressVSwitch_Unknown));

		break;
	}

#endif // (NTDDI_VERSION >= NTDDI_WIN8)
#endif // (NTDDI_VERSION >= NTDDI_WIN7)

	}

	return;
}

/**
 @private_function="PrvAdvancedPacketInjectionCountersIncrementTotalActionResults"

   Purpose:  Increment the appropriate counters based on the layerId, direction, and action.    <br>
																								<br>
   Notes:                                                                                       <br>
																								<br>
   MSDN_Ref:                                                                                    <br>
*/
VOID PrvAdvancedPacketInjectionCountersIncrementTotalActionResults(_In_ const FWPS_INCOMING_VALUES* pClassifyValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* pMetadataValues,
	_In_ const FWPS_CLASSIFY_OUT* pClassifyOut)
{
	INJECTION_COUNTERS* pCounters = 0;

	if (pClassifyOut->actionType == FWP_ACTION_BLOCK)
	{
		NT_ASSERT(pClassifyOut->flags & FWPS_CLASSIFY_OUT_FLAG_ABSORB);

		pCounters = &g_apiTotalBlockedAndAbsorbed;
	}
	else
		pCounters = &g_apiTotalPermitted;

	PrvAdvancedPacketInjectionCountersIncrement(pClassifyValues,
		pMetadataValues,
		pCounters);

	return;
}

/**
 @function="AdvancedPacketInjectionCountersIncrement"

   Purpose:  Increment the appropriate counters based on the injection handle.                  <br>
																								<br>
   Notes:                                                                                       <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Desktop/MS683615.aspx                      <br>
*/
VOID AdvancedPacketInjectionCountersIncrement(_In_ HANDLE injectionHandle,
	_Inout_ INJECTION_COUNTERS* pCounters)
{
	if (injectionHandle == g_pIPv4InboundNetworkInjectionHandles[0] ||
		injectionHandle == g_pIPv4InboundNetworkInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->inboundNetwork_IPv4));
	else if (injectionHandle == g_pIPv6InboundNetworkInjectionHandles[0] ||
		injectionHandle == g_pIPv6InboundNetworkInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->inboundNetwork_IPv6));
	else if (injectionHandle == g_pIPv4OutboundNetworkInjectionHandles[0] ||
		injectionHandle == g_pIPv4OutboundNetworkInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->outboundNetwork_IPv4));
	else if (injectionHandle == g_pIPv6OutboundNetworkInjectionHandles[0] ||
		injectionHandle == g_pIPv6OutboundNetworkInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->outboundNetwork_IPv6));
	else if (injectionHandle == g_pIPv4InboundForwardInjectionHandles[0] ||
		injectionHandle == g_pIPv4InboundForwardInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->inboundForward_IPv4));
	else if (injectionHandle == g_pIPv6InboundForwardInjectionHandles[0] ||
		injectionHandle == g_pIPv6InboundForwardInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->inboundForward_IPv6));
	else if (injectionHandle == g_pIPv4OutboundForwardInjectionHandles[0] ||
		injectionHandle == g_pIPv4OutboundForwardInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->outboundForward_IPv4));
	else if (injectionHandle == g_pIPv6OutboundForwardInjectionHandles[0] ||
		injectionHandle == g_pIPv6OutboundForwardInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->outboundForward_IPv6));
	else if (injectionHandle == g_pIPv4InboundTransportInjectionHandles[0] ||
		injectionHandle == g_pIPv4InboundTransportInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv4));
	else if (injectionHandle == g_pIPv6InboundTransportInjectionHandles[0] ||
		injectionHandle == g_pIPv6InboundTransportInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->inboundTransport_IPv6));
	else if (injectionHandle == g_pIPv4OutboundTransportInjectionHandles[0] ||
		injectionHandle == g_pIPv4OutboundTransportInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv4));
	else if (injectionHandle == g_pIPv6OutboundTransportInjectionHandles[0] ||
		injectionHandle == g_pIPv6OutboundTransportInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->outboundTransport_IPv6));

#if(NTDDI_VERSION >= NTDDI_WIN8)

	else if (injectionHandle == g_pIPv4InboundMACInjectionHandles[0] ||
		injectionHandle == g_pIPv4InboundMACInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->inboundMAC_IPv4));
	else if (injectionHandle == g_pIPv6InboundMACInjectionHandles[0] ||
		injectionHandle == g_pIPv6InboundMACInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->inboundMAC_IPv6));
	else if (injectionHandle == g_pInboundMACInjectionHandles[0] ||
		injectionHandle == g_pInboundMACInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->inboundMAC_Unknown));
	else if (injectionHandle == g_pIPv4OutboundMACInjectionHandles[0] ||
		injectionHandle == g_pIPv4OutboundMACInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->outboundMAC_IPv4));
	else if (injectionHandle == g_pIPv6OutboundMACInjectionHandles[0] ||
		injectionHandle == g_pIPv6OutboundMACInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->outboundMAC_IPv6));
	else if (injectionHandle == g_pOutboundMACInjectionHandles[0] ||
		injectionHandle == g_pOutboundMACInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->outboundMAC_Unknown));
	else if (injectionHandle == g_pIPv4IngressVSwitchEthernetInjectionHandles[0] ||
		injectionHandle == g_pIPv4IngressVSwitchEthernetInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->ingressVSwitch_IPv4));
	else if (injectionHandle == g_pIPv6IngressVSwitchEthernetInjectionHandles[0] ||
		injectionHandle == g_pIPv6IngressVSwitchEthernetInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->ingressVSwitch_IPv6));
	else if (injectionHandle == g_pIngressVSwitchEthernetInjectionHandles[0] ||
		injectionHandle == g_pIngressVSwitchEthernetInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->ingressVSwitch_Unknown));
	else if (injectionHandle == g_pIPv4EgressVSwitchEthernetInjectionHandles[0] ||
		injectionHandle == g_pIPv4EgressVSwitchEthernetInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->egressVSwitch_IPv4));
	else if (injectionHandle == g_pIPv6EgressVSwitchEthernetInjectionHandles[0] ||
		injectionHandle == g_pIPv6EgressVSwitchEthernetInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->egressVSwitch_IPv6));
	else if (injectionHandle == g_pEgressVSwitchEthernetInjectionHandles[0] ||
		injectionHandle == g_pEgressVSwitchEthernetInjectionHandles[1])
		InterlockedIncrement64((LONG64*)&(pCounters->egressVSwitch_Unknown));

#endif /// (NTDDI_VERSION >= NTDDI_WIN8)

	return;
}

#endif /// DBG

#if(NTDDI_VERSION >= NTDDI_WIN8)

/**
 @private_function="PerformAdvancedPacketInjectionAtInboundMACFrame"

   Purpose:  Creates a new NET_BUFFER_LIST and injects it in to the stack from the incoming MAC
			 Layers using FwpsInjectMacReceiveAsync().                                          <br>
																								<br>
   Notes:    Applies to the following inbound layers:                                           <br>
				FWPM_LAYER_INBOUND_MAC_FRAME_ETHERNET                                           <br>
				FWPM_LAYER_INBOUND_MAC_FRAME_NATIVE                                             <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/HH439588.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF546324.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS PerformAdvancedPacketInjectionAtInboundMACFrame(_In_ CLASSIFY_DATA** ppClassifyData,
	_In_ INJECTION_DATA** ppInjectionData,
	_In_ BOOLEAN isInline = FALSE)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> PerformAdvancedPacketInjectionAtInboundMACFrame()\n");

#endif /// DBG

	NT_ASSERT(ppClassifyData);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppClassifyData);
	NT_ASSERT(*ppInjectionData);

	NTSTATUS                                   status = STATUS_SUCCESS;
	FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)(*ppClassifyData)->pClassifyValues;
	FWPS_INCOMING_METADATA_VALUES* pMetadata = (FWPS_INCOMING_METADATA_VALUES*)(*ppClassifyData)->pMetadataValues;
	PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)(*ppClassifyData)->pFilter->providerContext->dataBuffer->data;
	IF_INDEX                                   interfaceIndex = 0;
	NDIS_PORT_NUMBER                           ndisPort = 0;
	NET_BUFFER_LIST* pNetBufferList = 0;
	UINT32                                     size = 0;
	ADVANCED_PACKET_INJECTION_COMPLETION_DATA* pCompletionData = 0;
	UINT32                                     bytesRetreated = 0;
	FWP_VALUE* pInterfaceIndex = 0;
	FWP_VALUE* pNDISPort = 0;

#if DBG

	KIRQL                                      irql = KeGetCurrentIrql();
	HANDLE                                     injectionHandle = (*ppInjectionData)->injectionHandle;

#endif /// DBG

#pragma warning(push)
#pragma warning(disable: 6014) /// pCompletionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pCompletionData,
		ADVANCED_PACKET_INJECTION_COMPLETION_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pCompletionData,
		status);

#pragma warning(pop)

	KeInitializeSpinLock(&(pCompletionData->spinLock));

	pCompletionData->performedInline = isInline;
	pCompletionData->pClassifyData = *ppClassifyData;
	pCompletionData->pInjectionData = *ppInjectionData;

	/// Responsibility for freeing this memory has been transferred to the pCompletionData
	*ppClassifyData = 0;

	*ppInjectionData = 0;

	pInterfaceIndex = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_INTERFACE_INDEX);
	if (pInterfaceIndex &&
		pInterfaceIndex->type == FWP_UINT32)
		interfaceIndex = (IF_INDEX)pInterfaceIndex->uint32;

	pNDISPort = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_NDIS_PORT);
	if (pNDISPort &&
		pNDISPort->type == FWP_UINT32)
		ndisPort = (NDIS_PORT_NUMBER)pNDISPort->uint32;

	/// If NATIVE, initial offset is at the MAC Header ...
	if (pClassifyValues->layerId != FWPS_LAYER_INBOUND_MAC_FRAME_NATIVE &&
		FWPS_IS_L2_METADATA_FIELD_PRESENT(pMetadata,
			FWPS_L2_METADATA_FIELD_ETHERNET_MAC_HEADER_SIZE))
		bytesRetreated = pMetadata->ethernetMacHeaderSize;

	if (bytesRetreated)
	{
		/// ... otherwise the offset is at the IP Header, so retreat the size of the MAC Header ...
		status = NdisRetreatNetBufferDataStart(NET_BUFFER_LIST_FIRST_NB((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket),
			bytesRetreated,
			0,
			0);
		if (status != STATUS_SUCCESS)
		{
			DbgPrintEx(DPFLTR_IHVNETWORK_ID,
				DPFLTR_ERROR_LEVEL,
				" !!!! PerformAdvancedPacketInjectionAtInboundMACFrame: NdisRetreatNetBufferDataStart() [status: %#x]\n",
				status);

			HLPR_BAIL;
		}
	}

	/// ... create a new NET_BUFFER_LIST based on the original NET_BUFFER_LIST ...
	pNetBufferList = KrnlHlprNBLCreateNew(g_pNDISPoolData->nblPoolHandle,
		(NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		&(pCompletionData->pAllocatedBuffer),
		&size,
		&(pCompletionData->pAllocatedMDL),
		pData->additionalBytes,
		FALSE);

	if (bytesRetreated)
	{
		/// ... and advance the offset back to the original position.
		NdisAdvanceNetBufferDataStart(NET_BUFFER_LIST_FIRST_NB((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket),
			bytesRetreated,
			FALSE,
			0);
	}

	if (!pNetBufferList)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtInboundMACFrame: KrnlHlprNBLCreateNew() [pNetBufferList: %#p]\n",
			pNetBufferList);

		HLPR_BAIL;
	}

#if DBG

	AdvancedPacketInjectionCountersIncrement(injectionHandle,
		&g_apiOutstandingNewNBLs);

#endif /// DBG

	pCompletionData->refCount = KrnlHlprNBLGetRequiredRefCount(pNetBufferList,
		TRUE);

	status = FwpsInjectMacReceiveAsync(pCompletionData->pInjectionData->injectionHandle,
		pCompletionData->pInjectionData->injectionContext,
		0,
		pClassifyValues->layerId,
		interfaceIndex,
		ndisPort,
		pNetBufferList,
		CompleteAdvancedPacketInjection,
		pCompletionData);

	NT_ASSERT(irql == KeGetCurrentIrql());

	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtInboundMACFrame: FwpsInjectMacReceiveAsync() [status: %#x]\n",
			status);

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalFailedInjectionCalls);

#endif /// DBG

	}

#if DBG

	else
		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalSuccessfulInjectionCalls);

#endif /// DBG

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS)
	{
		if (pNetBufferList)
		{
			FwpsFreeNetBufferList(pNetBufferList);

			pNetBufferList = 0;

#if DBG

			AdvancedPacketInjectionCountersDecrement(injectionHandle,
				&g_apiOutstandingNewNBLs);

#endif

		}

		if (pCompletionData)
			AdvancedPacketInjectionCompletionDataDestroy(&pCompletionData,
				TRUE);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- PerformAdvancedPacketInjectionAtInboundMACFrame() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

/**
 @private_function="PerformAdvancedPacketInjectionAtOutboundMACFrame"

   Purpose:  Creates a new NET_BUFFER_LIST and injects it in to the stack from the outgoing MAC
			 Layers using FwpsInjectMacSendAsync().                                             <br>
																								<br>
   Notes:    Applies to the following outbound layers:                                          <br>
				FWPM_LAYER_OUTBOUND_MAC_FRAME_ETHERNET                                          <br>
				FWPM_LAYER_OUTBOUND_MAC_FRAME_NATIVE                                            <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/HH439593.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF546324.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS PerformAdvancedPacketInjectionAtOutboundMACFrame(_In_ CLASSIFY_DATA** ppClassifyData,
	_In_ INJECTION_DATA** ppInjectionData,
	_In_ BOOLEAN isInline = FALSE)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> PerformAdvancedPacketInjectionAtOutboundMACFrame()\n");

#endif /// DBG

	NT_ASSERT(ppClassifyData);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppClassifyData);
	NT_ASSERT(*ppInjectionData);

	NTSTATUS                                   status = STATUS_SUCCESS;
	FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)(*ppClassifyData)->pClassifyValues;
	PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)(*ppClassifyData)->pFilter->providerContext->dataBuffer->data;
	IF_INDEX                                   interfaceIndex = 0;
	NDIS_PORT_NUMBER                           ndisPort = 0;
	NET_BUFFER_LIST* pNetBufferList = 0;
	UINT32                                     size = 0;
	ADVANCED_PACKET_INJECTION_COMPLETION_DATA* pCompletionData = 0;
	FWP_VALUE* pInterfaceIndex = 0;
	FWP_VALUE* pNDISPort = 0;

#if DBG

	KIRQL                                      irql = KeGetCurrentIrql();
	HANDLE                                     injectionHandle = (*ppInjectionData)->injectionHandle;

#endif /// DBG

#pragma warning(push)
#pragma warning(disable: 6014) /// pCompletionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pCompletionData,
		ADVANCED_PACKET_INJECTION_COMPLETION_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pCompletionData,
		status);

#pragma warning(pop)

	KeInitializeSpinLock(&(pCompletionData->spinLock));

	pCompletionData->performedInline = isInline;
	pCompletionData->pClassifyData = *ppClassifyData;
	pCompletionData->pInjectionData = *ppInjectionData;

	/// Responsibility for freeing this memory has been transferred to the pCompletionData
	*ppClassifyData = 0;

	*ppInjectionData = 0;

	pInterfaceIndex = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_INTERFACE_INDEX);
	if (pInterfaceIndex &&
		pInterfaceIndex->type == FWP_UINT32)
		interfaceIndex = (IF_INDEX)pInterfaceIndex->uint32;

	pNDISPort = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_NDIS_PORT);
	if (pNDISPort &&
		pNDISPort->type == FWP_UINT32)
		ndisPort = (NDIS_PORT_NUMBER)pNDISPort->uint32;

	/// Initial offset is at the MAC Header, so just create a new NET_BUFFER_LIST based on the 
	/// original NET_BUFFER_LIST ...
	pNetBufferList = KrnlHlprNBLCreateNew(g_pNDISPoolData->nblPoolHandle,
		(NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		&(pCompletionData->pAllocatedBuffer),
		&size,
		&(pCompletionData->pAllocatedMDL),
		pData->additionalBytes,
		FALSE);

	if (!pNetBufferList)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtOutboundMACFrame: KrnlHlprNBLCreateNew() [pNetBufferList: %#p]\n",
			pNetBufferList);

		HLPR_BAIL;
	}

#if DBG

	AdvancedPacketInjectionCountersIncrement(injectionHandle,
		&g_apiOutstandingNewNBLs);

#endif /// DBG

	pCompletionData->refCount = KrnlHlprNBLGetRequiredRefCount(pNetBufferList,
		TRUE);

	status = FwpsInjectMacSendAsync(pCompletionData->pInjectionData->injectionHandle,
		pCompletionData->pInjectionData->injectionContext,
		0,
		pClassifyValues->layerId,
		interfaceIndex,
		ndisPort,
		pNetBufferList,
		CompleteAdvancedPacketInjection,
		pCompletionData);

	NT_ASSERT(irql == KeGetCurrentIrql());

	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtOutboundMACFrame: FwpsInjectMacSendAsync() [status: %#x]\n",
			status);

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalFailedInjectionCalls);

#endif /// DBG

	}

#if DBG

	else
		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalSuccessfulInjectionCalls);

#endif /// DBG


HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS)
	{
		if (pNetBufferList)
		{
			FwpsFreeNetBufferList(pNetBufferList);

			pNetBufferList = 0;

#if DBG

			AdvancedPacketInjectionCountersDecrement(injectionHandle,
				&g_apiOutstandingNewNBLs);

#endif

		}

		if (pCompletionData)
			AdvancedPacketInjectionCompletionDataDestroy(&pCompletionData,
				TRUE);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- PerformAdvancedPacketInjectionAtOutboundMACFrame() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

/**
 @private_function="PerformAdvancedPacketInjectionAtIngressVSwitchEthernet"

   Purpose:  Creates a new NET_BUFFER_LIST and injects it in to the virtual switch's ingress
			 path from the ingress VSwitch Layers using
			 FwpsInjectvSwitchEthernetIngressAsync0().                                          <br>
																								<br>
   Notes:    Applies to the following ingress layers:                                           <br>
				FWPM_LAYER_INGRESS_VSWITCH_ETHERNET                                             <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/HH439669.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF546324.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS PerformAdvancedPacketInjectionAtIngressVSwitchEthernet(_In_ CLASSIFY_DATA** ppClassifyData,
	_In_ INJECTION_DATA** ppInjectionData,
	_In_ BOOLEAN isInline = FALSE)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> PerformAdvancedPacketInjectionAtIngressVSwitchEthernet()\n");

#endif /// DBG

	NT_ASSERT(ppClassifyData);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppClassifyData);
	NT_ASSERT(*ppInjectionData);

	NTSTATUS                                   status = STATUS_SUCCESS;
	FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)(*ppClassifyData)->pClassifyValues;
	FWPS_INCOMING_METADATA_VALUES* pMetadata = (FWPS_INCOMING_METADATA_VALUES*)(*ppClassifyData)->pMetadataValues;
	PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)(*ppClassifyData)->pFilter->providerContext->dataBuffer->data;
	FWP_VALUE* pVSwitchIDValue = 0;
	FWP_BYTE_BLOB* pVSwitchID = 0;
	NDIS_SWITCH_PORT_ID                        sourcePortID = 0;
	NDIS_SWITCH_NIC_INDEX                      sourceNICIndex = 0;
	NET_BUFFER_LIST* pNetBufferList = 0;
	UINT32                                     size = 0;
	ADVANCED_PACKET_INJECTION_COMPLETION_DATA* pCompletionData = 0;

#if DBG

	KIRQL                                      irql = KeGetCurrentIrql();
	HANDLE                                     injectionHandle = (*ppInjectionData)->injectionHandle;

#endif /// DBG

#pragma warning(push)
#pragma warning(disable: 6014) /// pCompletionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pCompletionData,
		ADVANCED_PACKET_INJECTION_COMPLETION_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pCompletionData,
		status);

#pragma warning(pop)

	KeInitializeSpinLock(&(pCompletionData->spinLock));

	pCompletionData->performedInline = isInline;
	pCompletionData->pClassifyData = *ppClassifyData;
	pCompletionData->pInjectionData = *ppInjectionData;

	/// Responsibility for freeing this memory has been transferred to the pCompletionData
	*ppClassifyData = 0;

	*ppInjectionData = 0;

	if (FWPS_IS_L2_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_L2_METADATA_FIELD_VSWITCH_SOURCE_PORT_ID))
		sourcePortID = pMetadata->vSwitchSourcePortId;

	if (FWPS_IS_L2_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_L2_METADATA_FIELD_VSWITCH_SOURCE_NIC_INDEX))
		sourceNICIndex = (NDIS_SWITCH_NIC_INDEX)pMetadata->vSwitchSourceNicIndex;

	pVSwitchIDValue = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_VSWITCH_ID);
	if (pVSwitchIDValue)
		pVSwitchID = pVSwitchIDValue->byteBlob;

	if (pVSwitchID == 0)
	{
		status = STATUS_INVALID_MEMBER;

		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtIngressVSwitchEthernet() [status: %#x][pVSwitchID: %#p]\n",
			status,
			pVSwitchID);

		HLPR_BAIL;
	}

	/// Initial offset is at the MAC Header, so just create a new NET_BUFFER_LIST based on the 
	/// original NET_BUFFER_LIST ...
	pNetBufferList = KrnlHlprNBLCreateNew(g_pNDISPoolData->nblPoolHandle,
		(NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		&(pCompletionData->pAllocatedBuffer),
		&size,
		&(pCompletionData->pAllocatedMDL),
		pData->additionalBytes,
		FALSE);

	if (!pNetBufferList)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtIngressVSwitchEthernet: KrnlHlprNBLCreateNew() [pNetBufferList: %#p]\n",
			pNetBufferList);

		HLPR_BAIL;
	}

#if DBG

	AdvancedPacketInjectionCountersIncrement(injectionHandle,
		&g_apiOutstandingNewNBLs);

#endif /// DBG

	pCompletionData->refCount = KrnlHlprNBLGetRequiredRefCount(pNetBufferList,
		TRUE);

	status = FwpsInjectvSwitchEthernetIngressAsync(pCompletionData->pInjectionData->injectionHandle,
		pCompletionData->pInjectionData->injectionContext,
		0,
		0,
		pVSwitchID,
		sourcePortID,
		sourceNICIndex,
		pNetBufferList,
		CompleteAdvancedPacketInjection,
		pCompletionData);

	NT_ASSERT(irql == KeGetCurrentIrql());

	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtIngressVSwitchEthernet: FwpsInjectvSwitchEthernetIngressAsync() [status: %#x]\n",
			status);

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalFailedInjectionCalls);

#endif /// DBG

	}

#if DBG

	else
		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalSuccessfulInjectionCalls);

#endif /// DBG

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS)
	{
		if (pNetBufferList)
		{
			FwpsFreeNetBufferList(pNetBufferList);

			pNetBufferList = 0;

#if DBG

			AdvancedPacketInjectionCountersDecrement(injectionHandle,
				&g_apiOutstandingNewNBLs);

#endif

		}

		if (pCompletionData)
			AdvancedPacketInjectionCompletionDataDestroy(&pCompletionData,
				TRUE);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- PerformAdvancedPacketInjectionAtIngressVSwitchEthernet() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

/**
 @private_function="PerformAdvancedPacketInjectionAtEgressVSwitchEthernet"

   Purpose:  Creates a new NET_BUFFER_LIST and injects it in to the virtual switch's ingress
			 path from the egress VSwitch Layers using
			 FwpsInjectvSwitchEthernetIngressAsync0().                                          <br>
																								<br>
   Notes:    Applies to the following egress layers:                                            <br>
				FWPM_LAYER_EGRESS_VSWITCH_ETHERNET                                              <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/HH439662.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF546324.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS PerformAdvancedPacketInjectionAtEgressVSwitchEthernet(_In_ CLASSIFY_DATA** ppClassifyData,
	_In_ INJECTION_DATA** ppInjectionData,
	_In_ BOOLEAN isInline = FALSE)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> PerformAdvancedPacketInjectionAtEgressVSwitchEthernet()\n");

#endif /// DBG

	NT_ASSERT(ppClassifyData);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppClassifyData);
	NT_ASSERT(*ppInjectionData);

	NTSTATUS                                   status = STATUS_SUCCESS;
	FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)(*ppClassifyData)->pClassifyValues;
	FWPS_INCOMING_METADATA_VALUES* pMetadata = (FWPS_INCOMING_METADATA_VALUES*)(*ppClassifyData)->pMetadataValues;
	PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)(*ppClassifyData)->pFilter->providerContext->dataBuffer->data;
	FWP_VALUE* pVSwitchIDValue = 0;
	FWP_BYTE_BLOB* pVSwitchID = 0;
	NDIS_SWITCH_PORT_ID                        sourcePortID = 0;
	NDIS_SWITCH_NIC_INDEX                      sourceNICIndex = 0;
	NET_BUFFER_LIST* pNetBufferList = 0;
	UINT32                                     size = 0;
	ADVANCED_PACKET_INJECTION_COMPLETION_DATA* pCompletionData = 0;

#if DBG

	KIRQL                                   irql = KeGetCurrentIrql();
	HANDLE                                  injectionHandle = (*ppInjectionData)->injectionHandle;

#endif /// DBG

#pragma warning(push)
#pragma warning(disable: 6014) /// pCompletionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pCompletionData,
		ADVANCED_PACKET_INJECTION_COMPLETION_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pCompletionData,
		status);

#pragma warning(pop)

	KeInitializeSpinLock(&(pCompletionData->spinLock));

	pCompletionData->performedInline = isInline;
	pCompletionData->pClassifyData = *ppClassifyData;
	pCompletionData->pInjectionData = *ppInjectionData;

	/// Responsibility for freeing this memory has been transferred to the pCompletionData
	*ppClassifyData = 0;

	*ppInjectionData = 0;

	if (FWPS_IS_L2_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_L2_METADATA_FIELD_VSWITCH_SOURCE_PORT_ID))
		sourcePortID = pMetadata->vSwitchSourcePortId;

	if (FWPS_IS_L2_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_L2_METADATA_FIELD_VSWITCH_SOURCE_NIC_INDEX))
		sourceNICIndex = (NDIS_SWITCH_NIC_INDEX)pMetadata->vSwitchSourceNicIndex;

	pVSwitchIDValue = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_VSWITCH_ID);
	if (pVSwitchIDValue)
		pVSwitchID = pVSwitchIDValue->byteBlob;

	if (pVSwitchID == 0)
	{
		status = STATUS_INVALID_MEMBER;

		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtEgressVSwitchEthernet() [status: %#x][pVSwitchID: %#p]\n",
			status,
			pVSwitchID);

		HLPR_BAIL;
	}

	/// Initial offset is at the MAC Header, so just create a new NET_BUFFER_LIST based on the 
	/// original NET_BUFFER_LIST ...
	pNetBufferList = KrnlHlprNBLCreateNew(g_pNDISPoolData->nblPoolHandle,
		(NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		&(pCompletionData->pAllocatedBuffer),
		&size,
		&(pCompletionData->pAllocatedMDL),
		pData->additionalBytes,
		FALSE);

	if (!pNetBufferList)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtEgressVSwitchEthernet: KrnlHlprNBLCreateNew() [pNetBufferList: %#p]\n",
			pNetBufferList);

		HLPR_BAIL;
	}

#if DBG

	AdvancedPacketInjectionCountersIncrement(injectionHandle,
		&g_apiOutstandingNewNBLs);

#endif /// DBG

	pCompletionData->refCount = KrnlHlprNBLGetRequiredRefCount(pNetBufferList,
		TRUE);

	status = FwpsInjectvSwitchEthernetIngressAsync(pCompletionData->pInjectionData->injectionHandle,
		pCompletionData->pInjectionData->injectionContext,
		0,
		0,
		pVSwitchID,
		sourcePortID,
		sourceNICIndex,
		pNetBufferList,
		CompleteAdvancedPacketInjection,
		pCompletionData);

	NT_ASSERT(irql == KeGetCurrentIrql());

	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtEgressVSwitchEthernet: FwpsInjectvSwitchEthernetEgressAsync() [status: %#x]\n",
			status);

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalFailedInjectionCalls);

#endif /// DBG

	}

#if DBG

	else
		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalSuccessfulInjectionCalls);

#endif /// DBG

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS)
	{
		if (pNetBufferList)
		{
			FwpsFreeNetBufferList(pNetBufferList);

			pNetBufferList = 0;

#if DBG

			AdvancedPacketInjectionCountersDecrement(injectionHandle,
				&g_apiOutstandingNewNBLs);

#endif

		}

		if (pCompletionData)
			AdvancedPacketInjectionCompletionDataDestroy(&pCompletionData,
				TRUE);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- PerformAdvancedPacketInjectionAtEgressVSwitchEthernet() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

#endif // (NTDDI_VERSION >= NTDDI_WIN8)

/**
 @private_function="PerformAdvancedPacketInjectionAtInboundNetwork"

   Purpose:  Creates a new NET_BUFFER_LIST and injects it in to the stack's inbound path from
			 the incoming Network Layers using FwpsInjectNetworkReceiveAsync().                 <br>
																								<br>
   Notes:    Applies to the following inbound layers:                                           <br>
				FWPM_LAYER_INBOUND_IPPACKET_V{4/6}                                              <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF551183.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF546324.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS PerformAdvancedPacketInjectionAtInboundNetwork(_In_ CLASSIFY_DATA** ppClassifyData,
	_In_ INJECTION_DATA** ppInjectionData,
	_In_ BOOLEAN isInline = FALSE)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> PerformAdvancedPacketInjectionAtInboundNetwork()\n");

#endif /// DBG

	NT_ASSERT(ppClassifyData);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppClassifyData);
	NT_ASSERT(*ppInjectionData);

	NTSTATUS                                   status = STATUS_SUCCESS;
	FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)(*ppClassifyData)->pClassifyValues;
	FWPS_INCOMING_METADATA_VALUES* pMetadata = (FWPS_INCOMING_METADATA_VALUES*)(*ppClassifyData)->pMetadataValues;
	PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)(*ppClassifyData)->pFilter->providerContext->dataBuffer->data;
	COMPARTMENT_ID                             compartmentID = DEFAULT_COMPARTMENT_ID;
	IF_INDEX                                   interfaceIndex = 0;
	IF_INDEX                                   subInterfaceIndex = 0;
	UINT32                                     flags = 0;
	NET_BUFFER_LIST* pNetBufferList = 0;
	UINT32                                     size = 0;
	ADVANCED_PACKET_INJECTION_COMPLETION_DATA* pCompletionData = 0;
	UINT32                                     ipHeaderSize = 0;
	UINT32                                     bytesRetreated = 0;
	FWP_VALUE* pInterfaceIndex = 0;
	FWP_VALUE* pSubInterfaceIndex = 0;
	FWP_VALUE* pFlags = 0;
	NDIS_TCP_IP_CHECKSUM_PACKET_INFO           checksumInfo = { 0 };

#if DBG

	KIRQL                                      irql = KeGetCurrentIrql();
	HANDLE                                     injectionHandle = (*ppInjectionData)->injectionHandle;

#endif /// DBG

#pragma warning(push)
#pragma warning(disable: 6014) /// pCompletionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pCompletionData,
		ADVANCED_PACKET_INJECTION_COMPLETION_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pCompletionData,
		status);

#pragma warning(pop)

	KeInitializeSpinLock(&(pCompletionData->spinLock));

	pCompletionData->performedInline = isInline;
	pCompletionData->pClassifyData = *ppClassifyData;
	pCompletionData->pInjectionData = *ppInjectionData;

	/// Responsibility for freeing this memory has been transferred to the pCompletionData
	*ppClassifyData = 0;

	*ppInjectionData = 0;

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_COMPARTMENT_ID))
		compartmentID = (COMPARTMENT_ID)pMetadata->compartmentId;

	pInterfaceIndex = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_INTERFACE_INDEX);
	if (pInterfaceIndex &&
		pInterfaceIndex->type == FWP_UINT32)
		interfaceIndex = (IF_INDEX)pInterfaceIndex->uint32;

	pSubInterfaceIndex = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_SUB_INTERFACE_INDEX);
	if (pSubInterfaceIndex &&
		pSubInterfaceIndex->type == FWP_UINT32)
		subInterfaceIndex = (IF_INDEX)pSubInterfaceIndex->uint32;

	pFlags = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_FLAGS);
	if (pFlags &&
		pFlags->type == FWP_UINT32)
		flags = pFlags->uint32;

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_IP_HEADER_SIZE))
		bytesRetreated = ipHeaderSize = pMetadata->ipHeaderSize;

	checksumInfo.Value = (ULONG)(ULONG_PTR)NET_BUFFER_LIST_INFO((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		TcpIpChecksumNetBufferListInfo);

	/// Initial offset is at the Transport Header, so retreat the size of the IP Header ...
	status = NdisRetreatNetBufferDataStart(NET_BUFFER_LIST_FIRST_NB((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket),
		bytesRetreated,
		0,
		0);
	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtInboundNetwork: NdisRetreatNetBufferDataStart() [status: %#x]\n",
			status);

		HLPR_BAIL;
	}

	/// ... create a new NET_BUFFER_LIST based on the original NET_BUFFER_LIST ...
	pNetBufferList = KrnlHlprNBLCreateNew(g_pNDISPoolData->nblPoolHandle,
		(NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		&(pCompletionData->pAllocatedBuffer),
		&size,
		&(pCompletionData->pAllocatedMDL),
		pData->additionalBytes,
		FALSE);

	/// ... and advance the offset back to the original position.
	NdisAdvanceNetBufferDataStart(NET_BUFFER_LIST_FIRST_NB((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket),
		bytesRetreated,
		FALSE,
		0);

	if (!pNetBufferList)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtInboundNetwork: KrnlHlprNBLCreateNew() [pNetBufferList: %#p]\n",
			pNetBufferList);

		HLPR_BAIL;
	}

	/// Handle if this packet had the IP checksum offloaded or if it's loopback
	if (checksumInfo.Receive.NdisPacketIpChecksumSucceeded ||
		flags & FWP_CONDITION_FLAG_IS_LOOPBACK)
	{
		/// Prevent TCP/IP Zone crossing and recalculate the checksums
		if (flags & FWP_CONDITION_FLAG_IS_LOOPBACK)
		{
			FWP_VALUE* pLocalAddress = 0;
			FWP_VALUE* pRemoteAddress = 0;
			FWP_VALUE* pLoopbackAddress = 0;

			pLocalAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
				&FWPM_CONDITION_IP_REMOTE_ADDRESS);
			if (pLocalAddress &&
				((pLocalAddress->type == FWP_UINT32 &&
					RtlCompareMemory(&(pLocalAddress->uint32),
						IPV4_LOOPBACK_ADDRESS,
						IPV4_ADDRESS_SIZE)) ||
					(pLocalAddress->type == FWP_BYTE_ARRAY16_TYPE &&
						RtlCompareMemory(pLocalAddress->byteArray16->byteArray16,
							IPV6_LOOPBACK_ADDRESS,
							IPV6_ADDRESS_SIZE))))
				pLoopbackAddress = pLocalAddress;

			if (!pLoopbackAddress)
			{
				pRemoteAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
					&FWPM_CONDITION_IP_REMOTE_ADDRESS);
				if (pRemoteAddress &&
					((pRemoteAddress->type == FWP_UINT32 &&
						RtlCompareMemory(&(pRemoteAddress->uint32),
							IPV4_LOOPBACK_ADDRESS,
							IPV4_ADDRESS_SIZE)) ||
						(pRemoteAddress->type == FWP_BYTE_ARRAY16_TYPE &&
							RtlCompareMemory(pRemoteAddress->byteArray16->byteArray16,
								IPV6_LOOPBACK_ADDRESS,
								IPV6_ADDRESS_SIZE))))
					pLoopbackAddress = pRemoteAddress;
			}

			if (pLoopbackAddress)
			{
				status = KrnlHlprIPHeaderModifyLoopbackToLocal(pMetadata,
					pLoopbackAddress,
					ipHeaderSize,
					pNetBufferList,
					(const WSACMSGHDR*)pCompletionData->pInjectionData->pControlData,
					pCompletionData->pInjectionData->controlDataLength);
				if (status != STATUS_SUCCESS)
				{
					DbgPrintEx(DPFLTR_IHVNETWORK_ID,
						DPFLTR_ERROR_LEVEL,
						" !!!! PerformAdvancedPacketInjectionAtInboundNetwork: KrnlHlprIPHeaderModifyLoopbackToLocal() [status: %#x]\n",
						status);

					HLPR_BAIL;
				}
			}
		}
		else
		{
			/// Recalculate the checksum
			if (pCompletionData->pInjectionData->addressFamily == AF_INET)
				KrnlHlprIPHeaderCalculateV4Checksum(pNetBufferList,
					ipHeaderSize);
		}
	}

#if DBG

	AdvancedPacketInjectionCountersIncrement(injectionHandle,
		&g_apiOutstandingNewNBLs);

#endif /// DBG

	pCompletionData->refCount = KrnlHlprNBLGetRequiredRefCount(pNetBufferList);

	/// Handle if this packet is destined for the software loopback
	if (flags & FWP_CONDITION_FLAG_IS_LOOPBACK)
	{
		FWP_VALUE* pLocalAddress = 0;
		FWP_VALUE* pRemoteAddress = 0;
		FWP_VALUE* pLoopbackAddress = 0;

		pLocalAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
			&FWPM_CONDITION_IP_REMOTE_ADDRESS);
		if (pLocalAddress &&
			((pLocalAddress->type == FWP_UINT32 &&
				RtlCompareMemory(&(pLocalAddress->uint32),
					IPV4_LOOPBACK_ADDRESS,
					IPV4_ADDRESS_SIZE)) ||
				(pLocalAddress->type == FWP_BYTE_ARRAY16_TYPE &&
					RtlCompareMemory(pLocalAddress->byteArray16->byteArray16,
						IPV6_LOOPBACK_ADDRESS,
						IPV6_ADDRESS_SIZE))))
			pLoopbackAddress = pLocalAddress;

		if (!pLoopbackAddress)
		{
			pRemoteAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
				&FWPM_CONDITION_IP_REMOTE_ADDRESS);
			if (pRemoteAddress &&
				((pRemoteAddress->type == FWP_UINT32 &&
					RtlCompareMemory(&(pRemoteAddress->uint32),
						IPV4_LOOPBACK_ADDRESS,
						IPV4_ADDRESS_SIZE)) ||
					(pRemoteAddress->type == FWP_BYTE_ARRAY16_TYPE &&
						RtlCompareMemory(pRemoteAddress->byteArray16->byteArray16,
							IPV6_LOOPBACK_ADDRESS,
							IPV6_ADDRESS_SIZE))))
				pLoopbackAddress = pRemoteAddress;
		}

		if (pLoopbackAddress)
		{
			status = KrnlHlprIPHeaderModifyLoopbackToLocal(pMetadata,
				pLoopbackAddress,
				ipHeaderSize,
				pNetBufferList,
				(const WSACMSGHDR*)pCompletionData->pInjectionData->pControlData,
				pCompletionData->pInjectionData->controlDataLength);
			if (status != STATUS_SUCCESS)
			{
				DbgPrintEx(DPFLTR_IHVNETWORK_ID,
					DPFLTR_ERROR_LEVEL,
					" !!!! PerformAdvancedPacketInjectionAtInboundNetwork: KrnlHlprIPHeaderModifyLoopbackToLocal() [status: %#x]\n",
					status);

				HLPR_BAIL;
			}
		}
	}

	status = FwpsInjectNetworkReceiveAsync(pCompletionData->pInjectionData->injectionHandle,
		pCompletionData->pInjectionData->injectionContext,
		0,
		compartmentID,
		interfaceIndex,
		subInterfaceIndex,
		pNetBufferList,
		CompleteAdvancedPacketInjection,
		pCompletionData);

	NT_ASSERT(irql == KeGetCurrentIrql());

	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtInboundNetwork: FwpsInjectNetworkReceiveAsync() [status: %#x]\n",
			status);

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalFailedInjectionCalls);

#endif /// DBG

	}

#if DBG

	else
		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalSuccessfulInjectionCalls);

#endif /// DBG

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS)
	{
		if (pNetBufferList)
		{
			FwpsFreeNetBufferList(pNetBufferList);

			pNetBufferList = 0;

#if DBG

			AdvancedPacketInjectionCountersDecrement(injectionHandle,
				&g_apiOutstandingNewNBLs);

#endif

		}

		if (pCompletionData)
			AdvancedPacketInjectionCompletionDataDestroy(&pCompletionData,
				TRUE);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- PerformAdvancedPacketInjectionAtInboundNetwork() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

/**
 @private_function="PerformAdvancedPacketInjectionAtOutboundNetwork"

   Purpose:  Creates a new NET_BUFFER_LIST and injects it in to the stack's outbound path
			 from the outgoing Network Layers using FwpsInjectNetworkSendAsync().               <br>
																								<br>
   Notes:    Applies to the following outbound layers:                                          <br>
				FWPM_LAYER_OUTBOUND_IPPACKET_V{4/6}                                             <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF551185.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF546324.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS PerformAdvancedPacketInjectionAtOutboundNetwork(_In_ CLASSIFY_DATA** ppClassifyData,
	_In_ INJECTION_DATA** ppInjectionData,
	_In_ BOOLEAN isInline = FALSE)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> PerformAdvancedPacketInjectionAtOutboundNetwork()\n");

#endif /// DBG

	NT_ASSERT(ppClassifyData);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppClassifyData);
	NT_ASSERT(*ppInjectionData);

	NTSTATUS                                   status = STATUS_SUCCESS;
	FWPS_INCOMING_METADATA_VALUES* pMetadata = (FWPS_INCOMING_METADATA_VALUES*)(*ppClassifyData)->pMetadataValues;
	PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)(*ppClassifyData)->pFilter->providerContext->dataBuffer->data;
	COMPARTMENT_ID                             compartmentID = DEFAULT_COMPARTMENT_ID;
	NET_BUFFER_LIST* pNetBufferList = 0;
	UINT32                                     size = 0;
	ADVANCED_PACKET_INJECTION_COMPLETION_DATA* pCompletionData = 0;

#if DBG

	KIRQL                                      irql = KeGetCurrentIrql();
	HANDLE                                     injectionHandle = (*ppInjectionData)->injectionHandle;

#endif /// DBG

#pragma warning(push)
#pragma warning(disable: 6014) /// pCompletionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pCompletionData,
		ADVANCED_PACKET_INJECTION_COMPLETION_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pCompletionData,
		status);

#pragma warning(pop)

	KeInitializeSpinLock(&(pCompletionData->spinLock));

	pCompletionData->performedInline = isInline;
	pCompletionData->pClassifyData = *ppClassifyData;
	pCompletionData->pInjectionData = *ppInjectionData;

	/// Responsibility for freeing this memory has been transferred to the pCompletionData
	*ppClassifyData = 0;

	*ppInjectionData = 0;

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_COMPARTMENT_ID))
		compartmentID = (COMPARTMENT_ID)pMetadata->compartmentId;

	/// Initial offset is at the IP Header, so just create a new NET_BUFFER_LIST based on the 
	/// original NET_BUFFER_LIST ...
	pNetBufferList = KrnlHlprNBLCreateNew(g_pNDISPoolData->nblPoolHandle,
		(NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		&(pCompletionData->pAllocatedBuffer),
		&size,
		&(pCompletionData->pAllocatedMDL),
		pData->additionalBytes,
		FALSE);

	if (!pNetBufferList)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtOutboundNetwork: KrnlHlprNBLCreateNew() [pNetBufferList: %#p]\n",
			pNetBufferList);

		HLPR_BAIL;
	}

#if DBG

	AdvancedPacketInjectionCountersIncrement(injectionHandle,
		&g_apiOutstandingNewNBLs);

#endif /// DBG

	pCompletionData->refCount = KrnlHlprNBLGetRequiredRefCount(pNetBufferList);

	status = FwpsInjectNetworkSendAsync(pCompletionData->pInjectionData->injectionHandle,
		pCompletionData->pInjectionData->injectionContext,
		0,
		compartmentID,
		pNetBufferList,
		CompleteAdvancedPacketInjection,
		pCompletionData);

	NT_ASSERT(irql == KeGetCurrentIrql());

	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtOutboundNetwork: FwpsInjectNetworkSendAsync() [status: %#x]\n",
			status);

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalFailedInjectionCalls);

#endif /// DBG

	}

#if DBG

	else
		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalSuccessfulInjectionCalls);

#endif /// DBG

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS)
	{
		if (pNetBufferList)
		{
			FwpsFreeNetBufferList(pNetBufferList);

			pNetBufferList = 0;

#if DBG

			AdvancedPacketInjectionCountersDecrement(injectionHandle,
				&g_apiOutstandingNewNBLs);

#endif

		}

		if (pCompletionData)
			AdvancedPacketInjectionCompletionDataDestroy(&pCompletionData,
				TRUE);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- PerformAdvancedPacketInjectionAtOutboundNetwork() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

/**
 @private_function="PerformAdvancedPacketInjectionAtForward"

   Purpose:  Creates a new NET_BUFFER_LIST and injects it in to the stack's forward path using
			 FwpsInjectForwardAsync().                                                          <br>
																								<br>
   Notes:    Applies to the following forwarding layers:                                        <br>
				FWPM_LAYER_IPFORWARD_V{4/6}                                                     <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF551186.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF546324.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS PerformAdvancedPacketInjectionAtForward(_In_ CLASSIFY_DATA** ppClassifyData,
	_In_ INJECTION_DATA** ppInjectionData,
	_In_ BOOLEAN isInline = FALSE)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> PerformAdvancedPacketInjectionAtForward()\n");

#endif /// DBG

	NT_ASSERT(ppClassifyData);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppClassifyData);
	NT_ASSERT(*ppInjectionData);

	NTSTATUS                                   status = STATUS_SUCCESS;
	FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)(*ppClassifyData)->pClassifyValues;
	FWPS_INCOMING_METADATA_VALUES* pMetadata = (FWPS_INCOMING_METADATA_VALUES*)(*ppClassifyData)->pMetadataValues;
	PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)(*ppClassifyData)->pFilter->providerContext->dataBuffer->data;
	COMPARTMENT_ID                             compartmentID = DEFAULT_COMPARTMENT_ID;
	IF_INDEX                                   interfaceIndex = 0;
	UINT32                                     flags = 0;
	NET_BUFFER_LIST* pNetBufferList = 0;
	UINT32                                     size = 0;
	ADVANCED_PACKET_INJECTION_COMPLETION_DATA* pCompletionData = 0;
	UINT32                                     ipHeaderSize = 0;
	FWP_VALUE* pInterfaceIndex = 0;
	FWP_VALUE* pFlags = 0;
	BOOLEAN                                    isWeakHostReceive = FALSE;
	BOOLEAN                                    isWeakHostSend = FALSE;
	PSTR                                       pInjectionFn = "FwpsInjectForwardAsync";
	NDIS_TCP_IP_CHECKSUM_PACKET_INFO           checksumInfo = { 0 };

#if DBG

	KIRQL                                      irql = KeGetCurrentIrql();
	HANDLE                                     injectionHandle = (*ppInjectionData)->injectionHandle;

#endif /// DBG

#pragma warning(push)
#pragma warning(disable: 6014) /// pCompletionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pCompletionData,
		ADVANCED_PACKET_INJECTION_COMPLETION_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pCompletionData,
		status);

#pragma warning(pop)

	KeInitializeSpinLock(&(pCompletionData->spinLock));

	pCompletionData->performedInline = isInline;
	pCompletionData->pClassifyData = *ppClassifyData;
	pCompletionData->pInjectionData = *ppInjectionData;

	/// Responsibility for freeing this memory has been transferred to the pCompletionData
	*ppClassifyData = 0;

	*ppInjectionData = 0;

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_COMPARTMENT_ID))
		compartmentID = (COMPARTMENT_ID)pMetadata->compartmentId;

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_IP_HEADER_SIZE))
		ipHeaderSize = pMetadata->ipHeaderSize;

	pInterfaceIndex = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_DESTINATION_INTERFACE_INDEX);
	if (pInterfaceIndex &&
		pInterfaceIndex->type == FWP_UINT32)
		interfaceIndex = (IF_INDEX)pInterfaceIndex->uint32;

	pFlags = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_FLAGS);
	if (pFlags &&
		pFlags->type == FWP_UINT32)
		flags = pFlags->uint32;

#if(NTDDI_VERSION >= NTDDI_WIN7)

	/// Determine if this is a weakhost forward
	if (flags & FWP_CONDITION_FLAG_IS_INBOUND_PASS_THRU)
		isWeakHostReceive = TRUE;

	if (flags & FWP_CONDITION_FLAG_IS_OUTBOUND_PASS_THRU)
		isWeakHostSend = TRUE;

#endif /// (NTDDI_VERSION >= NTDDI_WIN7)

	/// Initial offset is at the IP Header, so just create a new NET_BUFFER_LIST based on the 
	/// original NET_BUFFER_LIST ...
	pNetBufferList = KrnlHlprNBLCreateNew(g_pNDISPoolData->nblPoolHandle,
		(NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		&(pCompletionData->pAllocatedBuffer),
		&size,
		&(pCompletionData->pAllocatedMDL),
		pData->additionalBytes,
		FALSE);

	if (!pNetBufferList)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtForward: KrnlHlprNBLCreateNew() [pNetBufferList: %#p]\n",
			pNetBufferList);

		HLPR_BAIL;
	}
#if DBG

	AdvancedPacketInjectionCountersIncrement(injectionHandle,
		&g_apiOutstandingNewNBLs);

#endif /// DBG

	checksumInfo.Value = (ULONG)(ULONG_PTR)NET_BUFFER_LIST_INFO((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		TcpIpChecksumNetBufferListInfo);

	/// Handle if this packet had the IP checksum offloaded or if it's loopback
	if (checksumInfo.Receive.NdisPacketIpChecksumSucceeded ||
		flags & FWP_CONDITION_FLAG_IS_LOOPBACK)
	{
		/// Prevent TCP/IP Zone crossing and recalculate the checksums
		if (flags & FWP_CONDITION_FLAG_IS_LOOPBACK)
		{
			FWP_VALUE* pLocalAddress = 0;
			FWP_VALUE* pRemoteAddress = 0;
			FWP_VALUE* pLoopbackAddress = 0;

			pLocalAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
				&FWPM_CONDITION_IP_REMOTE_ADDRESS);
			if (pLocalAddress &&
				((pLocalAddress->type == FWP_UINT32 &&
					RtlCompareMemory(&(pLocalAddress->uint32),
						IPV4_LOOPBACK_ADDRESS,
						IPV4_ADDRESS_SIZE)) ||
					(pLocalAddress->type == FWP_BYTE_ARRAY16_TYPE &&
						RtlCompareMemory(pLocalAddress->byteArray16->byteArray16,
							IPV6_LOOPBACK_ADDRESS,
							IPV6_ADDRESS_SIZE))))
				pLoopbackAddress = pLocalAddress;

			if (!pLoopbackAddress)
			{
				pRemoteAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
					&FWPM_CONDITION_IP_REMOTE_ADDRESS);
				if (pRemoteAddress &&
					((pRemoteAddress->type == FWP_UINT32 &&
						RtlCompareMemory(&(pRemoteAddress->uint32),
							IPV4_LOOPBACK_ADDRESS,
							IPV4_ADDRESS_SIZE)) ||
						(pRemoteAddress->type == FWP_BYTE_ARRAY16_TYPE &&
							RtlCompareMemory(pRemoteAddress->byteArray16->byteArray16,
								IPV6_LOOPBACK_ADDRESS,
								IPV6_ADDRESS_SIZE))))
					pLoopbackAddress = pRemoteAddress;
			}

			if (pLoopbackAddress)
			{
				status = KrnlHlprIPHeaderModifyLoopbackToLocal(pMetadata,
					pLoopbackAddress,
					ipHeaderSize,
					pNetBufferList,
					(const WSACMSGHDR*)pCompletionData->pInjectionData->pControlData,
					pCompletionData->pInjectionData->controlDataLength);
				if (status != STATUS_SUCCESS)
				{
					DbgPrintEx(DPFLTR_IHVNETWORK_ID,
						DPFLTR_ERROR_LEVEL,
						" !!!! PerformAdvancedPacketInjectionAtForward: KrnlHlprIPHeaderModifyLoopbackToLocal() [status: %#x]\n",
						status);

					HLPR_BAIL;
				}
			}
		}
		else
		{
			/// Recalculate the checksum
			if (pCompletionData->pInjectionData->addressFamily == AF_INET)
				KrnlHlprIPHeaderCalculateV4Checksum(pNetBufferList,
					ipHeaderSize);
		}
	}

	pCompletionData->refCount = KrnlHlprNBLGetRequiredRefCount(pNetBufferList);

	/// If the Forwarded NBL is destined locally, inject using FwpsInjectNetworkReceiveAsync rather 
	/// than the traditional FwpsInjectForwardAsync otherwise STATUS_INVALID_PARAMETER will be 
	/// returned in the NBL.status and the injection fails.
	if (isWeakHostReceive)
	{
		UINT32     index = WFPSAMPLER_INDEX;
		IF_INDEX   subInterfaceIndex = 0;
		FWP_VALUE* pSubInterfaceIndex = 0;

		if (pCompletionData->pClassifyData->pFilter->subLayerWeight == FWPM_SUBLAYER_UNIVERSAL_WEIGHT)
			index = UNIVERSAL_INDEX;

#if DBG

		if (injectionHandle == g_pIPv4InboundForwardInjectionHandles[0] ||
			injectionHandle == g_pIPv4InboundForwardInjectionHandles[1])
			InterlockedDecrement64((LONG64*)&(g_apiOutstandingNewNBLs.inboundForward_IPv4));
		else if (injectionHandle == g_pIPv6InboundForwardInjectionHandles[0] ||
			injectionHandle == g_pIPv6InboundForwardInjectionHandles[1])
			InterlockedDecrement64((LONG64*)&(g_apiOutstandingNewNBLs.inboundForward_IPv6));

#endif /// DBG

		if (pCompletionData->pInjectionData->addressFamily == AF_INET)
			pCompletionData->pInjectionData->injectionHandle = g_pIPv4InboundNetworkInjectionHandles[index];
		else
			pCompletionData->pInjectionData->injectionHandle = g_pIPv6InboundNetworkInjectionHandles[index];

		pSubInterfaceIndex = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
			&FWPM_CONDITION_DESTINATION_SUB_INTERFACE_INDEX);
		if (pSubInterfaceIndex &&
			pSubInterfaceIndex->type == FWP_UINT32)
			subInterfaceIndex = (IF_INDEX)pSubInterfaceIndex->uint32;

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiOutstandingNewNBLs);

#endif /// DBG

		status = FwpsInjectNetworkReceiveAsync(pCompletionData->pInjectionData->injectionHandle,
			pCompletionData->pInjectionData->injectionContext,
			0,
			compartmentID,
			interfaceIndex,
			subInterfaceIndex,
			pNetBufferList,
			CompleteAdvancedPacketInjection,
			pCompletionData);
	}
	/// If the Forwarded NBL is sourced locally, but another interface, inject using 
	/// FwpsInjectNetworkSendAsync rather than the traditional FwpsInjectForwardAsync otherwise 
	/// STATUS_INVALID_PARAMETER will be returned in the NBL.status and the injection fails
	else if (isWeakHostSend)
	{
		UINT32 index = WFPSAMPLER_INDEX;

		if (pCompletionData->pClassifyData->pFilter->subLayerWeight == FWPM_SUBLAYER_UNIVERSAL_WEIGHT)
			index = UNIVERSAL_INDEX;

#if DBG

		if (injectionHandle == g_pIPv4OutboundForwardInjectionHandles[0] ||
			injectionHandle == g_pIPv4OutboundForwardInjectionHandles[1])
			InterlockedDecrement64((LONG64*)&(g_apiOutstandingNewNBLs.outboundForward_IPv4));
		else if (injectionHandle == g_pIPv6OutboundForwardInjectionHandles[0] ||
			injectionHandle == g_pIPv6OutboundForwardInjectionHandles[1])
			InterlockedDecrement64((LONG64*)&(g_apiOutstandingNewNBLs.outboundForward_IPv6));

#endif /// DBG

		if (pCompletionData->pInjectionData->addressFamily == AF_INET)
			pCompletionData->pInjectionData->injectionHandle = g_pIPv4OutboundNetworkInjectionHandles[index];
		else
			pCompletionData->pInjectionData->injectionHandle = g_pIPv6OutboundNetworkInjectionHandles[index];

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiOutstandingNewNBLs);

#endif /// DBG

		status = FwpsInjectNetworkSendAsync(pCompletionData->pInjectionData->injectionHandle,
			pCompletionData->pInjectionData->injectionContext,
			0,
			compartmentID,
			pNetBufferList,
			CompleteAdvancedPacketInjection,
			pCompletionData);
	}
	else
		status = FwpsInjectForwardAsync(pCompletionData->pInjectionData->injectionHandle,
			pCompletionData->pInjectionData->injectionContext,
			0,
			pCompletionData->pInjectionData->addressFamily,
			compartmentID,
			interfaceIndex,
			pNetBufferList,
			CompleteAdvancedPacketInjection,
			pCompletionData);

	NT_ASSERT(irql == KeGetCurrentIrql());

	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtForward: %s() [status: %#x]\n",
			pInjectionFn,
			status);

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalFailedInjectionCalls);

#endif /// DBG

	}

#if DBG

	else
		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalSuccessfulInjectionCalls);

#endif /// DBG

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS)
	{
		if (pNetBufferList)
		{
			FwpsFreeNetBufferList(pNetBufferList);

			pNetBufferList = 0;

#if DBG

			AdvancedPacketInjectionCountersDecrement(injectionHandle,
				&g_apiOutstandingNewNBLs);

#endif

		}

		if (pCompletionData)
			AdvancedPacketInjectionCompletionDataDestroy(&pCompletionData,
				TRUE);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- PerformAdvancedPacketInjectionAtForward() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

/**
 @private_function="PerformAdvancedPacketInjectionAtInboundTransport"

   Purpose:  Creates a new NET_BUFFER_LIST and injects it in to the stack's inbound path from
			 the incoming Transport Layers using FwpsInjectTransportRecveiveAsync().            <br>
																								<br>
   Notes:    Applies to the following inbound layers:                                           <br>
				FWPM_LAYER_INBOUND_TRANSPORT_V{4/6}                                             <br>
				FWPM_LAYER_INBOUND_ICMP_ERROR_V{4/6}                                            <br>
				FWPM_LAYER_DATAGRAM_DATA_V{4/6}        (Inbound only)                           <br>
				FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V{4/6} (Inbound only)                           <br>
				FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V{4/6} (Inbound only)                           <br>
				FWPM_LAYER_ALE_AUTH_CONNECT_V{4/6}     (Inbound, reauthorization only)          <br>
				FWPM_LAYER_ALE_FLOW_ESTABLISHED_V{4/6} (Inbound, non-TCP only)                  <br>
				FWPM_LAYER_STREAM_PACKET_V{4/6}        (Inbound only)                           <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF551186.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF546324.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS PerformAdvancedPacketInjectionAtInboundTransport(_In_ CLASSIFY_DATA** ppClassifyData,
	_In_ INJECTION_DATA** ppInjectionData,
	_In_ BOOLEAN isInline = FALSE)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> PerformAdvancedPacketInjectionAtInboundTransport()\n");

#endif /// DBG

	NT_ASSERT(ppClassifyData);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppClassifyData);
	NT_ASSERT(*ppInjectionData);

	NTSTATUS                                   status = STATUS_SUCCESS;
	FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)(*ppClassifyData)->pClassifyValues;
	FWPS_INCOMING_METADATA_VALUES* pMetadata = (FWPS_INCOMING_METADATA_VALUES*)(*ppClassifyData)->pMetadataValues;
	PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)(*ppClassifyData)->pFilter->providerContext->dataBuffer->data;
	COMPARTMENT_ID                             compartmentID = DEFAULT_COMPARTMENT_ID;
	IF_INDEX                                   interfaceIndex = 0;
	IF_INDEX                                   subInterfaceIndex = 0;
	UINT32                                     flags = 0;
	NET_BUFFER_LIST* pNetBufferList = 0;
	UINT32                                     size = 0;
	ADVANCED_PACKET_INJECTION_COMPLETION_DATA* pCompletionData = 0;
	UINT32                                     ipHeaderSize = 0;
	UINT32                                     transportHeaderSize = 0;
	UINT32                                     bytesRetreated = 0;
	IPPROTO                                    protocol = IPPROTO_MAX;
	FWP_VALUE* pProtocol = 0;
	FWP_VALUE* pInterfaceIndex = 0;
	FWP_VALUE* pSubInterfaceIndex = 0;
	FWP_VALUE* pFlags = 0;
	FWPS_PACKET_LIST_INFORMATION* pPacketInformation = 0;
	BOOLEAN                                    bypassInjection = FALSE;
	BYTE* pSourceAddress = 0;
	BYTE* pDestinationAddress = 0;
	NDIS_TCP_IP_CHECKSUM_PACKET_INFO           checksumInfo = { 0 };

#if DBG

	KIRQL                                      irql = KeGetCurrentIrql();
	HANDLE                                     injectionHandle = (*ppInjectionData)->injectionHandle;

#endif /// DBG

#pragma warning(push)
#pragma warning(disable: 6014) /// pCompletionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pCompletionData,
		ADVANCED_PACKET_INJECTION_COMPLETION_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pCompletionData,
		status);

#pragma warning(pop)

	KeInitializeSpinLock(&(pCompletionData->spinLock));

	pCompletionData->performedInline = isInline;
	pCompletionData->pClassifyData = *ppClassifyData;
	pCompletionData->pInjectionData = *ppInjectionData;

	/// Responsibility for freeing this memory has been transferred to the pCompletionData
	*ppClassifyData = 0;

	*ppInjectionData = 0;

	HLPR_NEW(pPacketInformation,
		FWPS_PACKET_LIST_INFORMATION,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pPacketInformation,
		status);
	pInterfaceIndex = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_INTERFACE_INDEX);
	if (pInterfaceIndex &&
		pInterfaceIndex->type == FWP_UINT32)
		interfaceIndex = (IF_INDEX)pInterfaceIndex->uint32;

	pSubInterfaceIndex = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_SUB_INTERFACE_INDEX);
	if (pSubInterfaceIndex &&
		pSubInterfaceIndex->type == FWP_UINT32)
		subInterfaceIndex = (IF_INDEX)pSubInterfaceIndex->uint32;

	pFlags = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_FLAGS);
	if (pFlags &&
		pFlags->type == FWP_UINT32)
		flags = pFlags->uint32;

	if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V4)
		protocol = IPPROTO_ICMP;
	else if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V6)
		protocol = IPPROTO_ICMPV6;

#if(NTDDI_VERSION >= NTDDI_WIN7)

	else if (pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6)
		protocol = IPPROTO_TCP;

#endif /// (NTDDI_VERSION >= NTDDI_WIN7)

	else
	{
		pProtocol = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
			&FWPM_CONDITION_IP_PROTOCOL);
		HLPR_BAIL_ON_NULL_POINTER(pProtocol);

		protocol = (IPPROTO)pProtocol->uint8;
	}

	if (pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4)
	{
		ipHeaderSize = IPV4_HEADER_MIN_SIZE;

		if (protocol == IPPROTO_ICMP)
			transportHeaderSize = ICMP_HEADER_MIN_SIZE;
		else if (protocol == IPPROTO_TCP)
			transportHeaderSize = TCP_HEADER_MIN_SIZE;
		else if (protocol == IPPROTO_UDP)
			transportHeaderSize = UDP_HEADER_MIN_SIZE;
	}
	else if (pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6)
	{
		ipHeaderSize = IPV6_HEADER_MIN_SIZE;

		if (protocol == IPPROTO_ICMPV6)
			transportHeaderSize = ICMP_HEADER_MIN_SIZE;
		else if (protocol == IPPROTO_TCP)
			transportHeaderSize = TCP_HEADER_MIN_SIZE;
		else if (protocol == IPPROTO_UDP)
			transportHeaderSize = UDP_HEADER_MIN_SIZE;
	}

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_COMPARTMENT_ID))
		compartmentID = (COMPARTMENT_ID)pMetadata->compartmentId;

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_IP_HEADER_SIZE) &&
		pMetadata->ipHeaderSize)
		ipHeaderSize = pMetadata->ipHeaderSize;

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_TRANSPORT_HEADER_SIZE) &&
		pMetadata->transportHeaderSize)
		transportHeaderSize = pMetadata->transportHeaderSize;

	bytesRetreated = ipHeaderSize;

	if (protocol != IPPROTO_ICMP &&
		protocol != IPPROTO_ICMPV6)
	{
		if (!isInline &&
			protocol != IPPROTO_TCP &&
			!(protocol == IPPROTO_UDP &&
				flags & FWP_CONDITION_FLAG_IS_RAW_ENDPOINT) &&
			(pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6))
		{
			/// For asynchronous execution, the drop will cause the stack to continue processing on the 
			/// NBL for auditing purposes.  This processing retreats the NBL Offset to the Transport header.
			/// We need to take this into account because we only took a reference on the NBL.
		}
		else
			bytesRetreated += transportHeaderSize;
	}
	else
	{
		if (pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V6)
		{
			if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
				FWPS_METADATA_FIELD_TRANSPORT_HEADER_SIZE))
				bytesRetreated += pMetadata->transportHeaderSize;
		}
	}

	/// Query to see if IPsec has applied tunnel mode SA's to this NET_BUFFER_LIST ...
	status = FwpsGetPacketListSecurityInformation((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		FWPS_PACKET_LIST_INFORMATION_QUERY_ALL_INBOUND,
		pPacketInformation);
	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtInboundTransport: FwpsGetPacketListSecurityInformation() [status: %#x]\n",
			status);

		HLPR_BAIL;
	}

	/// ... if it has, then bypass the injection until the NET_BUFFER_LIST has come out of the tunnel
	if ((pPacketInformation->ipsecInformation.inbound.isTunnelMode &&
		!(pPacketInformation->ipsecInformation.inbound.isDeTunneled)) ||
		pPacketInformation->ipsecInformation.inbound.isSecure)
	{
		bypassInjection = TRUE;

		HLPR_BAIL;
	}

	/// Initial offset is at the data, so retreat the size of the IP Header and Transport Header ...
	/// For ICMP, offset is at the ICMP Header, so retreat the size of the IP Header ...
	status = NdisRetreatNetBufferDataStart(NET_BUFFER_LIST_FIRST_NB((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket),
		bytesRetreated,
		0,
		0);
	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtInboundTransport: NdisRetreatNetBufferDataStart() [status: %#x]\n",
			status);

		HLPR_BAIL;
	}

	/// ... create a new NET_BUFFER_LIST based on the original NET_BUFFER_LIST ...
	pNetBufferList = KrnlHlprNBLCreateNew(g_pNDISPoolData->nblPoolHandle,
		(NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		&(pCompletionData->pAllocatedBuffer),
		&size,
		&(pCompletionData->pAllocatedMDL),
		pData->additionalBytes,
		FALSE);

	/// ... and advance the offset back to the original position.
	NdisAdvanceNetBufferDataStart(NET_BUFFER_LIST_FIRST_NB((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket),
		bytesRetreated,
		FALSE,
		0);

	if (!pNetBufferList)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtInboundTransport: KrnlHlprNBLCreateNew() [pNetBufferList: %#p]\n",
			pNetBufferList);

		HLPR_BAIL;
	}

#if DBG

	AdvancedPacketInjectionCountersIncrement(injectionHandle,
		&g_apiOutstandingNewNBLs);

#endif /// DBG

	checksumInfo.Value = (ULONG)(ULONG_PTR)NET_BUFFER_LIST_INFO((NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		TcpIpChecksumNetBufferListInfo);

	/// Handle if the packet was IPsec secured
	if (pCompletionData->pInjectionData->isIPsecSecured)
	{
		/// For performance reasons, IPsec leaves the original ESP / AH information in the IP Header ...
		UINT32     headerIncludeSize = 0;
		UINT64     endpointHandle = 0;
		UINT32     ipv4Address = 0;
		UINT32     addressSize = 0;
		FWP_VALUE* pRemoteAddressValue = 0;
		FWP_VALUE* pLocalAddressValue = 0;
		FWP_VALUE* pProtocolValue = 0;

		pRemoteAddressValue = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
			&FWPM_CONDITION_IP_REMOTE_ADDRESS);
		if (pRemoteAddressValue)
		{
			if (pRemoteAddressValue->type == FWP_BYTE_ARRAY16_TYPE)
				addressSize = IPV6_ADDRESS_SIZE;
			else
				addressSize = IPV4_ADDRESS_SIZE;

			HLPR_NEW_ARRAY(pSourceAddress,
				BYTE,
				addressSize,
				WFPSAMPLER_CALLOUT_DRIVER_TAG);
			HLPR_BAIL_ON_ALLOC_FAILURE(pSourceAddress,
				status);

			if (pRemoteAddressValue->type == FWP_BYTE_ARRAY16_TYPE)
				RtlCopyMemory(pSourceAddress,
					pRemoteAddressValue->byteArray16->byteArray16,
					addressSize);
			else
			{
				ipv4Address = htonl(pRemoteAddressValue->uint32);

				RtlCopyMemory(pSourceAddress,
					&ipv4Address,
					addressSize);
			}
		}

		pLocalAddressValue = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
			&FWPM_CONDITION_IP_LOCAL_ADDRESS);
		if (pLocalAddressValue)
		{
			if (pLocalAddressValue->type == FWP_BYTE_ARRAY16_TYPE)
				addressSize = IPV6_ADDRESS_SIZE;
			else
				addressSize = IPV4_ADDRESS_SIZE;

			HLPR_NEW_ARRAY(pDestinationAddress,
				BYTE,
				addressSize,
				WFPSAMPLER_CALLOUT_DRIVER_TAG);
			HLPR_BAIL_ON_ALLOC_FAILURE(pDestinationAddress,
				status);

			if (pLocalAddressValue->type == FWP_BYTE_ARRAY16_TYPE)
				RtlCopyMemory(pDestinationAddress,
					pLocalAddressValue->byteArray16->byteArray16,
					addressSize);
			else
			{
				ipv4Address = htonl(pLocalAddressValue->uint32);

				RtlCopyMemory(pDestinationAddress,
					&ipv4Address,
					addressSize);
			}
		}

		pProtocolValue = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
			&FWPM_CONDITION_IP_PROTOCOL);
		if (pProtocolValue &&
			pProtocolValue->type == FWP_UINT8)
			protocol = (IPPROTO)pProtocolValue->uint8;
		else
			protocol = IPPROTO_MAX;

		NT_ASSERT(protocol != IPPROTO_MAX);

#if (NTDDI_VERSION >= NTDDI_WIN6SP1)

		if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
			FWPS_METADATA_FIELD_TRANSPORT_HEADER_INCLUDE_HEADER))
			headerIncludeSize = pMetadata->headerIncludeHeaderLength;

#endif // (NTDDI_VERSION >= NTDDI_WIN6SP1)

		if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
			FWPS_METADATA_FIELD_TRANSPORT_ENDPOINT_HANDLE))
			endpointHandle = pMetadata->transportEndpointHandle;

		if (pSourceAddress == 0 ||
			pDestinationAddress == 0)
		{
			status = STATUS_INVALID_MEMBER;

			DbgPrintEx(DPFLTR_IHVNETWORK_ID,
				DPFLTR_ERROR_LEVEL,
				" !!!! PerformAdvancedPacketModificationAtInboundTransport() [status: %#x][pSourceAddress: %#p][pDestinationAddress: %#p]\n",
				status,
				pSourceAddress,
				pDestinationAddress);

			HLPR_BAIL;
		}

		/// ... so we must re-construct the IPHeader with the appropriate information
		status = FwpsConstructIpHeaderForTransportPacket(pNetBufferList,
			headerIncludeSize,
			pCompletionData->pInjectionData->addressFamily,
			pSourceAddress,
			pDestinationAddress,
			protocol,
			endpointHandle,
			(const WSACMSGHDR*)pCompletionData->pInjectionData->pControlData,
			pCompletionData->pInjectionData->controlDataLength,
			0,
			0,
			interfaceIndex,
			subInterfaceIndex);
		if (status != STATUS_SUCCESS)
		{
			DbgPrintEx(DPFLTR_IHVNETWORK_ID,
				DPFLTR_ERROR_LEVEL,
				" !!!! PerformAdvancedPacketInjectionAtInboundTransport: FwpsConstructIpHeaderForTransportPacket() [status: %#x]\n",
				status);

			HLPR_BAIL;
		}
	}
	/// Handle if this packet had the IP or Transport checksums offloaded or if it's loopback
	else if (checksumInfo.Receive.NdisPacketIpChecksumSucceeded ||
		checksumInfo.Receive.NdisPacketTcpChecksumSucceeded ||
		checksumInfo.Receive.NdisPacketUdpChecksumSucceeded ||
		flags & FWP_CONDITION_FLAG_IS_LOOPBACK)
	{
		/// Prevent TCP/IP Zone crossing and recalculate the checksums
		if (flags & FWP_CONDITION_FLAG_IS_LOOPBACK)
		{
			FWP_VALUE* pLocalAddress = 0;
			FWP_VALUE* pRemoteAddress = 0;
			FWP_VALUE* pLoopbackAddress = 0;

			pLocalAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
				&FWPM_CONDITION_IP_REMOTE_ADDRESS);
			if (pLocalAddress &&
				((pLocalAddress->type == FWP_UINT32 &&
					RtlCompareMemory(&(pLocalAddress->uint32),
						IPV4_LOOPBACK_ADDRESS,
						IPV4_ADDRESS_SIZE)) ||
					(pLocalAddress->type == FWP_BYTE_ARRAY16_TYPE &&
						RtlCompareMemory(pLocalAddress->byteArray16->byteArray16,
							IPV6_LOOPBACK_ADDRESS,
							IPV6_ADDRESS_SIZE))))
				pLoopbackAddress = pLocalAddress;

			if (!pLoopbackAddress)
			{
				pRemoteAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
					&FWPM_CONDITION_IP_REMOTE_ADDRESS);
				if (pRemoteAddress &&
					((pRemoteAddress->type == FWP_UINT32 &&
						RtlCompareMemory(&(pRemoteAddress->uint32),
							IPV4_LOOPBACK_ADDRESS,
							IPV4_ADDRESS_SIZE)) ||
						(pRemoteAddress->type == FWP_BYTE_ARRAY16_TYPE &&
							RtlCompareMemory(pRemoteAddress->byteArray16->byteArray16,
								IPV6_LOOPBACK_ADDRESS,
								IPV6_ADDRESS_SIZE))))
					pLoopbackAddress = pRemoteAddress;
			}

			if (pLoopbackAddress)
			{
				status = KrnlHlprIPHeaderModifyLoopbackToLocal(pMetadata,
					pLoopbackAddress,
					ipHeaderSize,
					pNetBufferList,
					(const WSACMSGHDR*)pCompletionData->pInjectionData->pControlData,
					pCompletionData->pInjectionData->controlDataLength);
				if (status != STATUS_SUCCESS)
				{
					DbgPrintEx(DPFLTR_IHVNETWORK_ID,
						DPFLTR_ERROR_LEVEL,
						" !!!! PerformAdvancedPacketInjectionAtInboundTransport: KrnlHlprIPHeaderModifyLoopbackToLocal() [status: %#x]\n",
						status);

					HLPR_BAIL;
				}
			}
		}
		else
		{
			/// Recalculate the checksum
			if (pCompletionData->pInjectionData->addressFamily == AF_INET)
				KrnlHlprIPHeaderCalculateV4Checksum(pNetBufferList,
					ipHeaderSize);
		}
	}

	pCompletionData->refCount = KrnlHlprNBLGetRequiredRefCount(pNetBufferList);

	status = FwpsInjectTransportReceiveAsync(pCompletionData->pInjectionData->injectionHandle,
		pCompletionData->pInjectionData->injectionContext,
		0,
		0,
		pCompletionData->pInjectionData->addressFamily,
		compartmentID,
		interfaceIndex,
		subInterfaceIndex,
		pNetBufferList,
		CompleteAdvancedPacketInjection,
		pCompletionData);

	NT_ASSERT(irql == KeGetCurrentIrql());

	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtInboundTransport: FwpsInjectTransportReceiveAsync() [status: %#x]\n",
			status);

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalFailedInjectionCalls);

#endif /// DBG

	}

#if DBG

	else
		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalSuccessfulInjectionCalls);

#endif /// DBG

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS ||
		bypassInjection)
	{
		if (pNetBufferList)
		{
			FwpsFreeNetBufferList(pNetBufferList);

			pNetBufferList = 0;

#if DBG

			AdvancedPacketInjectionCountersDecrement(injectionHandle,
				&g_apiOutstandingNewNBLs);

#endif

		}

		if (pCompletionData)
			AdvancedPacketInjectionCompletionDataDestroy(&pCompletionData,
				TRUE);
	}

	HLPR_DELETE_ARRAY(pSourceAddress,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);

	HLPR_DELETE_ARRAY(pDestinationAddress,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);

	HLPR_DELETE(pPacketInformation,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- PerformAdvancedPacketInjectionAtInboundTransport() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

/**
 @private_function="PerformAdvancedPacketInjectionAtOutboundTransport"

   Purpose:  Creates a new NET_BUFFER_LIST and injects it in to the stack's outbound path from
			 the outgoing Transport Layers using FwpsInjectTransportSendAsync().                <br>
																								<br>
   Notes:    Applies to the following outbound layers:                                          <br>
				FWPM_LAYER_OUTBOUND_TRANSPORT_V{4/6}                                            <br>
				FWPM_LAYER_OUTBOUND_ICMP_ERROR_V{4/6}                                           <br>
				FWPM_LAYER_DATAGRAM_DATA_V{4/6}        (Outbound only)                          <br>
				FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V{4/6} (Outbound only)                          <br>
				FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V{4/6} (Outbound only)                          <br>
				FWPM_LAYER_ALE_AUTH_CONNECT_V{4/6}     (Outbound reauthorization only)          <br>
				FWPM_LAYER_ALE_FLOW_ESTABLISHED_V{4/6} (Outbound, non-TCP only)                 <br>
				FWPM_LAYER_STREAM_PACKET_V{4/6}        (Outbound only)                          <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF551188.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF546324.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS PerformAdvancedPacketInjectionAtOutboundTransport(_In_ CLASSIFY_DATA** ppClassifyData,
	_In_ INJECTION_DATA** ppInjectionData,
	_In_ BOOLEAN isInline = FALSE)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> PerformAdvancedPacketInjectionAtOutboundTransport()\n");

#endif /// DBG

	NT_ASSERT(ppClassifyData);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppClassifyData);
	NT_ASSERT(*ppInjectionData);

	NTSTATUS                                   status = STATUS_SUCCESS;
	FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)(*ppClassifyData)->pClassifyValues;
	FWPS_INCOMING_METADATA_VALUES* pMetadata = (FWPS_INCOMING_METADATA_VALUES*)(*ppClassifyData)->pMetadataValues;
	PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)(*ppClassifyData)->pFilter->providerContext->dataBuffer->data;
	UINT64                                     endpointHandle = 0;
	FWPS_TRANSPORT_SEND_PARAMS* pSendParams = 0;
	COMPARTMENT_ID                             compartmentID = DEFAULT_COMPARTMENT_ID;
	NET_BUFFER_LIST* pNetBufferList = 0;
	UINT32                                     size = 0;
	ADVANCED_PACKET_INJECTION_COMPLETION_DATA* pCompletionData = 0;
	BYTE* pRemoteAddress = 0;
	FWP_VALUE* pAddressValue = 0;

#if DBG

	KIRQL                                      irql = KeGetCurrentIrql();
	HANDLE                                     injectionHandle = (*ppInjectionData)->injectionHandle;

#endif /// DBG

#pragma warning(push)
#pragma warning(disable: 6014) /// pCompletionData & pSendParams will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pCompletionData,
		ADVANCED_PACKET_INJECTION_COMPLETION_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pCompletionData,
		status);

	HLPR_NEW(pSendParams,
		FWPS_TRANSPORT_SEND_PARAMS,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pSendParams,
		status);

#pragma warning(pop)

	KeInitializeSpinLock(&(pCompletionData->spinLock));

	pCompletionData->performedInline = isInline;
	pCompletionData->pClassifyData = *ppClassifyData;
	pCompletionData->pInjectionData = *ppInjectionData;
	pCompletionData->pSendParams = pSendParams;

	/// Responsibility for freeing this memory has been transferred to the pCompletionData
	*ppClassifyData = 0;

	*ppInjectionData = 0;

	pSendParams = 0;

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_TRANSPORT_ENDPOINT_HANDLE))
		endpointHandle = pMetadata->transportEndpointHandle;

	if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
		FWPS_METADATA_FIELD_COMPARTMENT_ID))
		compartmentID = (COMPARTMENT_ID)pMetadata->compartmentId;

	pAddressValue = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
		&FWPM_CONDITION_IP_REMOTE_ADDRESS);
	if (pAddressValue)
	{
		if (pCompletionData->pInjectionData->addressFamily == AF_INET)
		{
			UINT32 tempAddress = htonl(pAddressValue->uint32);

			HLPR_NEW_ARRAY(pRemoteAddress,
				BYTE,
				IPV4_ADDRESS_SIZE,
				WFPSAMPLER_CALLOUT_DRIVER_TAG);
			HLPR_BAIL_ON_ALLOC_FAILURE(pRemoteAddress,
				status);

			RtlCopyMemory(pRemoteAddress,
				&tempAddress,
				IPV4_ADDRESS_SIZE);
		}
		else
		{
#pragma warning(push)
#pragma warning(disable: 6014) /// pRemoteAddress will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

			HLPR_NEW_ARRAY(pRemoteAddress,
				BYTE,
				IPV6_ADDRESS_SIZE,
				WFPSAMPLER_CALLOUT_DRIVER_TAG);
			HLPR_BAIL_ON_ALLOC_FAILURE(pRemoteAddress,
				status);

#pragma warning(pop)

			RtlCopyMemory(pRemoteAddress,
				pAddressValue->byteArray16->byteArray16,
				IPV6_ADDRESS_SIZE);

			if (FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
				FWPS_METADATA_FIELD_REMOTE_SCOPE_ID))
				pCompletionData->pSendParams->remoteScopeId = pMetadata->remoteScopeId;
		}

		pCompletionData->pSendParams->remoteAddress = pRemoteAddress;
	}

	pCompletionData->pSendParams->controlData = (WSACMSGHDR*)pCompletionData->pInjectionData->pControlData;
	pCompletionData->pSendParams->controlDataLength = pCompletionData->pInjectionData->controlDataLength;

	/// Initial offset is at Transport Header, so just create a new NET_BUFFER_LIST based on the 
	/// original NET_BUFFER_LIST ...
	pNetBufferList = KrnlHlprNBLCreateNew(g_pNDISPoolData->nblPoolHandle,
		(NET_BUFFER_LIST*)pCompletionData->pClassifyData->pPacket,
		&(pCompletionData->pAllocatedBuffer),
		&size,
		&(pCompletionData->pAllocatedMDL),
		pData->additionalBytes,
		FALSE);

	if (!pNetBufferList)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtOutboundTransport: KrnlHlprNBLCreateNew() [pNetBufferList: %#p]\n",
			pNetBufferList);

		HLPR_BAIL;
	}

#if DBG

	AdvancedPacketInjectionCountersIncrement(injectionHandle,
		&g_apiOutstandingNewNBLs);

#endif /// DBG

	pCompletionData->refCount = KrnlHlprNBLGetRequiredRefCount(pNetBufferList);

	status = FwpsInjectTransportSendAsync(pCompletionData->pInjectionData->injectionHandle,
		pCompletionData->pInjectionData->injectionContext,
		endpointHandle,
		0,
		pCompletionData->pSendParams,
		pCompletionData->pInjectionData->addressFamily,
		compartmentID,
		pNetBufferList,
		CompleteAdvancedPacketInjection,
		pCompletionData);

	NT_ASSERT(irql == KeGetCurrentIrql());

	if (status != STATUS_SUCCESS)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! PerformAdvancedPacketInjectionAtOutboundTransport: FwpsInjectTransportSendAsync() [status: %#x]\n",
			status);

#if DBG

		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalFailedInjectionCalls);

#endif /// DBG

	}

#if DBG

	else
		AdvancedPacketInjectionCountersIncrement(injectionHandle,
			&g_apiTotalSuccessfulInjectionCalls);

#endif /// DBG

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS)
	{
		if (pNetBufferList)
		{
			FwpsFreeNetBufferList(pNetBufferList);

			pNetBufferList = 0;

#if DBG

			AdvancedPacketInjectionCountersDecrement(injectionHandle,
				&g_apiOutstandingNewNBLs);

#endif

		}

		if (pCompletionData)
			AdvancedPacketInjectionCompletionDataDestroy(&pCompletionData,
				TRUE);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- PerformAdvancedPacketInjectionAtOutboundTransport() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

/**
 @private_function="AdvancedPacketInjectionDeferredProcedureCall"

   Purpose:  Invokes the appropriate private injection routine to perform the injection at
			 DISPATCH_LEVEL.                                                                    <br>
																								<br>
   Notes:                                                                                       <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF542972.aspx             <br>
*/
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Function_class_(KDEFERRED_ROUTINE)
VOID AdvancedPacketInjectionDeferredProcedureCall(_In_ KDPC* pDPC,
	_In_opt_ PVOID pContext,
	_In_opt_ PVOID pArg1,
	_In_opt_ PVOID pArg2)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> AdvancedPacketInjectionDeferredProcedureCall()\n");

#endif /// DBG

	UNREFERENCED_PARAMETER(pDPC);
	UNREFERENCED_PARAMETER(pContext);
	UNREFERENCED_PARAMETER(pArg2);

	NT_ASSERT(pDPC);
	NT_ASSERT(pArg1);
	NT_ASSERT(((DPC_DATA*)pArg1)->pClassifyData);
	NT_ASSERT(((DPC_DATA*)pArg1)->pInjectionData);

	DPC_DATA* pDPCData = (DPC_DATA*)pArg1;

	if (pDPCData)
	{
		NTSTATUS              status = STATUS_SUCCESS;
		FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)pDPCData->pClassifyData->pClassifyValues;

		if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V6)
			status = PerformAdvancedPacketInjectionAtInboundNetwork(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V6)
			status = PerformAdvancedPacketInjectionAtOutboundNetwork(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V6)
			status = PerformAdvancedPacketInjectionAtForward(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V6 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V6 ||
			(pDPCData->pInjectionData->direction == FWP_DIRECTION_INBOUND &&
				(pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V4 ||     /// Policy Change Reauthorization
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V6 ||     /// Policy Change Reauthorization
					pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6)))
			status = PerformAdvancedPacketInjectionAtInboundTransport(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V6 ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V6 ||
			(pDPCData->pInjectionData->direction == FWP_DIRECTION_OUTBOUND &&
				(pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 || /// Policy Change Reauthorization
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6 || /// Policy Change Reauthorization
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V6 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6)))
			status = PerformAdvancedPacketInjectionAtOutboundTransport(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);

#if(NTDDI_VERSION >= NTDDI_WIN7)

		else if (pDPCData->pInjectionData->direction == FWP_DIRECTION_INBOUND &&
			(pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6))
			status = PerformAdvancedPacketInjectionAtInboundTransport(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);
		else if (pDPCData->pInjectionData->direction == FWP_DIRECTION_OUTBOUND &&
			(pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6))
			status = PerformAdvancedPacketInjectionAtOutboundTransport(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);

#if(NTDDI_VERSION >= NTDDI_WIN8)

		else if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_MAC_FRAME_ETHERNET ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_MAC_FRAME_NATIVE)
			status = PerformAdvancedPacketInjectionAtInboundMACFrame(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_MAC_FRAME_ETHERNET ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_MAC_FRAME_NATIVE)
			status = PerformAdvancedPacketInjectionAtOutboundMACFrame(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_INGRESS_VSWITCH_ETHERNET)
			status = PerformAdvancedPacketInjectionAtIngressVSwitchEthernet(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_EGRESS_VSWITCH_ETHERNET)
			status = PerformAdvancedPacketInjectionAtEgressVSwitchEthernet(&(pDPCData->pClassifyData),
				&(pDPCData->pInjectionData),
				FALSE);

#endif // (NTDDI_VERSION >= NTDDI_WIN8)
#endif // (NTDDI_VERSION >= NTDDI_WIN7)

		else
			DbgPrintEx(DPFLTR_IHVNETWORK_ID,
				DPFLTR_ERROR_LEVEL,
				" !!!! AdvancedPacketInjectionDeferredProcedureCall() [status: %#x]\n",
				(UINT32)STATUS_NOT_SUPPORTED);

		if (status != STATUS_SUCCESS)
		{
			if (pDPCData->pClassifyData)
				KrnlHlprClassifyDataDestroyLocalCopy(&(pDPCData->pClassifyData));

			if (pDPCData->pInjectionData)
				KrnlHlprInjectionDataDestroy(&(pDPCData->pInjectionData));

			DbgPrintEx(DPFLTR_IHVNETWORK_ID,
				DPFLTR_ERROR_LEVEL,
				"  !!!! AdvancedPacketInjectionDeferredProcedureCall: PerformAdvancedPacketInjection() [status: %#x]\n",
				status);
		}

		KrnlHlprDPCDataDestroy(&pDPCData);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- AdvancedPacketInjectionDeferredProcedureCall()\n");

#endif /// DBG

	return;
}

/**
 @private_function="AdvancedPacketInjectionWorkItemRoutine"

   Purpose:  Invokes the appropriate private injection routine to perform the injection at
			 PASSIVE_LEVEL.                                                                     <br>
																								<br>
   Notes:                                                                                       <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF566380.aspx             <br>
*/
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(IO_WORKITEM_ROUTINE)
VOID AdvancedPacketInjectionWorkItemRoutine(_In_ PDEVICE_OBJECT pDeviceObject,
	_Inout_opt_ PVOID pContext)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> AdvancedPacketInjectionWorkItemRoutine()\n");

#endif /// DBG

	UNREFERENCED_PARAMETER(pDeviceObject);

	NT_ASSERT(pContext);
	NT_ASSERT(((WORKITEM_DATA*)pContext)->pClassifyData);
	NT_ASSERT(((WORKITEM_DATA*)pContext)->pInjectionData);

	WORKITEM_DATA* pWorkItemData = (WORKITEM_DATA*)pContext;

	if (pWorkItemData)
	{
		NTSTATUS              status = STATUS_SUCCESS;
		FWPS_INCOMING_VALUES* pClassifyValues = (FWPS_INCOMING_VALUES*)pWorkItemData->pClassifyData->pClassifyValues;

		if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V6)
			status = PerformAdvancedPacketInjectionAtInboundNetwork(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V6)
			status = PerformAdvancedPacketInjectionAtOutboundNetwork(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V6)
			status = PerformAdvancedPacketInjectionAtForward(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V6 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V6 ||
			(pWorkItemData->pInjectionData->direction == FWP_DIRECTION_INBOUND &&
				(pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V4 ||     /// Policy Change Reauthorization
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V6 ||     /// Policy Change Reauthorization
					pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6)))
			status = PerformAdvancedPacketInjectionAtInboundTransport(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V6 ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V6 ||
			(pWorkItemData->pInjectionData->direction == FWP_DIRECTION_OUTBOUND &&
				(pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 || /// Policy Change Reauthorization
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6 || /// Policy Change Reauthorization
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V6 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6)))
			status = PerformAdvancedPacketInjectionAtOutboundTransport(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);

#if(NTDDI_VERSION >= NTDDI_WIN7)

		else if (pWorkItemData->pInjectionData->direction == FWP_DIRECTION_INBOUND &&
			(pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6))
			status = PerformAdvancedPacketInjectionAtInboundTransport(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);
		else if (pWorkItemData->pInjectionData->direction == FWP_DIRECTION_OUTBOUND &&
			(pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6))
			status = PerformAdvancedPacketInjectionAtOutboundTransport(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);

#if(NTDDI_VERSION >= NTDDI_WIN8)

		else if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_MAC_FRAME_ETHERNET ||
			pClassifyValues->layerId == FWPS_LAYER_INBOUND_MAC_FRAME_NATIVE)
			status = PerformAdvancedPacketInjectionAtInboundMACFrame(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_MAC_FRAME_ETHERNET ||
			pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_MAC_FRAME_NATIVE)
			status = PerformAdvancedPacketInjectionAtOutboundMACFrame(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_INGRESS_VSWITCH_ETHERNET)
			status = PerformAdvancedPacketInjectionAtIngressVSwitchEthernet(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);
		else if (pClassifyValues->layerId == FWPS_LAYER_EGRESS_VSWITCH_ETHERNET)
			status = PerformAdvancedPacketInjectionAtEgressVSwitchEthernet(&(pWorkItemData->pClassifyData),
				&(pWorkItemData->pInjectionData),
				FALSE);

#endif // (NTDDI_VERSION >= NTDDI_WIN8)
#endif // (NTDDI_VERSION >= NTDDI_WIN7)

		else
			DbgPrintEx(DPFLTR_IHVNETWORK_ID,
				DPFLTR_ERROR_LEVEL,
				" !!!! AdvancedPacketInjectionWorkItemRoutine() [status: %#x]\n",
				(UINT32)STATUS_NOT_SUPPORTED);

		if (status != STATUS_SUCCESS)
		{
			if (pWorkItemData->pClassifyData)
				KrnlHlprClassifyDataDestroyLocalCopy(&(pWorkItemData->pClassifyData));

			if (pWorkItemData->pInjectionData)
				KrnlHlprInjectionDataDestroy(&(pWorkItemData->pInjectionData));

			DbgPrintEx(DPFLTR_IHVNETWORK_ID,
				DPFLTR_ERROR_LEVEL,
				"  !!!! AdvancedPacketInjectionWorkItemRoutine: PerformAdvancedPacketInjection() [status: %#x]\n",
				status);
		}

		KrnlHlprWorkItemDataDestroy(&pWorkItemData);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- AdvancedPacketInjectionWorkItemRoutine()\n");

#endif /// DBG

	return;
}

/**
 @private_function="TriggerAdvancedPacketInjectionInline"

   Purpose:  Makes a reference to all the classification data structures and invokes the
			 appropriate private injection routine to perform the injection.                    <br>
																								<br>
   Notes:                                                                                       <br>
																								<br>
   MSDN_Ref:                                                                                    <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS TriggerAdvancedPacketInjectionInline(_In_ const FWPS_INCOMING_VALUES* pClassifyValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* pMetadata,
	_Inout_opt_ VOID* pNetBufferList,
	_In_opt_ const VOID* pClassifyContext,
	_In_ const FWPS_FILTER* pFilter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* pClassifyOut,
	_In_ INJECTION_DATA** ppInjectionData)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> TriggerAdvancedPacketInjectionInline()\n");

#endif /// DBG

	NT_ASSERT(pClassifyValues);
	NT_ASSERT(pMetadata);
	NT_ASSERT(pNetBufferList);
	NT_ASSERT(pFilter);
	NT_ASSERT(pClassifyOut);
	NT_ASSERT(ppInjectionData);
	NT_ASSERT(*ppInjectionData);


	NTSTATUS       status = STATUS_SUCCESS;
	CLASSIFY_DATA* pClassifyData = 0;

#pragma warning(push)
#pragma warning(disable: 6014) /// pClassifyData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	HLPR_NEW(pClassifyData,
		CLASSIFY_DATA,
		WFPSAMPLER_CALLOUT_DRIVER_TAG);
	HLPR_BAIL_ON_ALLOC_FAILURE(pClassifyData,
		status);

#pragma warning(pop)

	pClassifyData->pClassifyValues = pClassifyValues;
	pClassifyData->pMetadataValues = pMetadata;
	pClassifyData->pPacket = pNetBufferList;
	pClassifyData->pClassifyContext = pClassifyContext;
	pClassifyData->pFilter = pFilter;
	pClassifyData->flowContext = flowContext;
	pClassifyData->pClassifyOut = pClassifyOut;

	if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V6)
		status = PerformAdvancedPacketInjectionAtInboundNetwork(&pClassifyData,
			ppInjectionData,
			TRUE);
	else if (pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V6)
		status = PerformAdvancedPacketInjectionAtOutboundNetwork(&pClassifyData,
			ppInjectionData,
			TRUE);
	else if (pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V6)
		status = PerformAdvancedPacketInjectionAtForward(&pClassifyData,
			ppInjectionData,
			TRUE);
	else if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V6 ||
		((*ppInjectionData)->direction == FWP_DIRECTION_INBOUND &&
			(pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
				pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6 ||
				pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V4 ||     /// Policy Change Reauthorization
				pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V6 ||     /// Policy Change Reauthorization
				pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6)))
		status = PerformAdvancedPacketInjectionAtInboundTransport(&pClassifyData,
			ppInjectionData,
			TRUE);
	else if (pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V6 ||
		((*ppInjectionData)->direction == FWP_DIRECTION_OUTBOUND &&
			(pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
				pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 || /// Policy Change Reauthorization
				pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6 || /// Policy Change Reauthorization
				pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V6 ||
				pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4 ||
				pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6)))
		status = PerformAdvancedPacketInjectionAtOutboundTransport(&pClassifyData,
			ppInjectionData,
			TRUE);

#if(NTDDI_VERSION >= NTDDI_WIN7)

	else if ((*ppInjectionData)->direction == FWP_DIRECTION_INBOUND &&
		(pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6))
		status = PerformAdvancedPacketInjectionAtInboundTransport(&pClassifyData,
			ppInjectionData,
			TRUE);
	else if ((*ppInjectionData)->direction == FWP_DIRECTION_OUTBOUND &&
		(pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
			pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6))
		status = PerformAdvancedPacketInjectionAtOutboundTransport(&pClassifyData,
			ppInjectionData,
			TRUE);

#if(NTDDI_VERSION >= NTDDI_WIN8)

	else if (pClassifyValues->layerId == FWPS_LAYER_INBOUND_MAC_FRAME_ETHERNET ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_MAC_FRAME_NATIVE)
		status = PerformAdvancedPacketInjectionAtInboundMACFrame(&pClassifyData,
			ppInjectionData,
			TRUE);
	else if (pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_MAC_FRAME_ETHERNET ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_MAC_FRAME_NATIVE)
		status = PerformAdvancedPacketInjectionAtOutboundMACFrame(&pClassifyData,
			ppInjectionData,
			TRUE);
	else if (pClassifyValues->layerId == FWPS_LAYER_INGRESS_VSWITCH_ETHERNET)
		status = PerformAdvancedPacketInjectionAtIngressVSwitchEthernet(&pClassifyData,
			ppInjectionData,
			TRUE);
	else if (pClassifyValues->layerId == FWPS_LAYER_EGRESS_VSWITCH_ETHERNET)
		status = PerformAdvancedPacketInjectionAtEgressVSwitchEthernet(&pClassifyData,
			ppInjectionData,
			TRUE);

#endif // (NTDDI_VERSION >= NTDDI_WIN8)
#endif // (NTDDI_VERSION >= NTDDI_WIN7)

	else
	{
		status = STATUS_NOT_SUPPORTED;

		DbgPrintEx(DPFLTR_IHVNETWORK_ID,
			DPFLTR_ERROR_LEVEL,
			" !!!! TriggerAdvancedPacketInjectionInline() [status: %#x]\n",
			status);
	}

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- TriggerAdvancedPacketInjectionInline() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

/**
 @private_function="TriggerAdvancedPacketInjectionOutOfBand"

   Purpose:  Creates a local copy of the classification data structures and queues a WorkItem
			 to perform the injection at PASSIVE_LEVEL.                                         <br>
																								<br>
   Notes:                                                                                       <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF550679.aspx             <br>
			 HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF566380.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Check_return_
NTSTATUS TriggerAdvancedPacketInjectionOutOfBand(_In_ const FWPS_INCOMING_VALUES* pClassifyValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* pMetadata,
	_Inout_opt_ VOID* pNetBufferList,
	_In_opt_ const VOID* pClassifyContext,
	_In_ const FWPS_FILTER* pFilter,
	_In_ UINT64 flowContext,
	_In_ FWPS_CLASSIFY_OUT* pClassifyOut,
	_In_ INJECTION_DATA* pInjectionData,
	_In_ PC_ADVANCED_PACKET_INJECTION_DATA* pPCData)
{
#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> TriggerAdvancedPacketInjectionOutOfBand()\n");

#endif /// DBG

	UNREFERENCED_PARAMETER(pClassifyOut);

	NT_ASSERT(pClassifyValues);
	NT_ASSERT(pMetadata);
	NT_ASSERT(pNetBufferList);
	NT_ASSERT(pFilter);
	NT_ASSERT(pClassifyOut);
	NT_ASSERT(pInjectionData);
	NT_ASSERT(pPCData);

	NTSTATUS       status = STATUS_SUCCESS;
	CLASSIFY_DATA* pClassifyData = 0;

#pragma warning(push)
#pragma warning(disable: 6014) /// pClassifyData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

	status = KrnlHlprClassifyDataCreateLocalCopy(&pClassifyData,
		pClassifyValues,
		pMetadata,
		pNetBufferList,
		pClassifyContext,
		pFilter,
		flowContext,
		pClassifyOut);
	HLPR_BAIL_ON_FAILURE(status);

#pragma warning(pop)

	if (pPCData->useWorkItems)
		status = KrnlHlprWorkItemQueue(g_pWDMDevice,
			AdvancedPacketInjectionWorkItemRoutine,
			pClassifyData,
			pInjectionData,
			0);
	else if (pPCData->useThreadedDPC)
		status = KrnlHlprThreadedDPCQueue(AdvancedPacketInjectionDeferredProcedureCall,
			pClassifyData,
			pInjectionData,
			0);
	else
		status = KrnlHlprDPCQueue(AdvancedPacketInjectionDeferredProcedureCall,
			pClassifyData,
			pInjectionData,
			0);

HLPR_BAIL_LABEL:

	NT_ASSERT(status == STATUS_SUCCESS);

	if (status != STATUS_SUCCESS)
	{
		if (pClassifyData)
			KrnlHlprClassifyDataDestroyLocalCopy(&pClassifyData);
	}

#if DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- TriggerAdvancedPacketInjectionOutOfBand() [status: %#x]\n",
		status);

#endif /// DBG

	return status;
}

#if(NTDDI_VERSION >= NTDDI_WIN7)

/**
 @classify_function="ClassifyAdvancedPacketInjection"

   Purpose:  Blocks the current NET_BUFFER_LIST and injects a new NBL back to the stack.        <br>
																								<br>
   Notes:    Applies to the following layers:                                                   <br>
				FWPS_LAYER_INBOUND_IPPACKET_V{4/6}                                              <br>
				FWPS_LAYER_OUTBOUND_IPPACKET_V{4/6}                                             <br>
				FWPS_LAYER_IPFORWARD_V{4/6}                                                     <br>
				FWPS_LAYER_INBOUND_TRANSPORT_V{4/6}                                             <br>
				FWPS_LAYER_OUTBOUND_TRANSPORT_V{4/6}                                            <br>
				FWPS_LAYER_DATAGRAM_DATA_V{4/6}                                                 <br>
				FWPS_LAYER_INBOUND_ICMP_ERROR_V{4/6}                                            <br>
				FWPS_LAYER_OUTBOUND_ICMP_ERROR_V{4/6}                                           <br>
				FWPS_LAYER_ALE_AUTH_CONNECT_V{4/6}                                              <br>
				FWPS_LAYER_ALE_FLOW_ESTABLISHED_V{4/6}                                          <br>
				FWPS_LAYER_STREAM_PACKET_V{4/6}                                                 <br>
				FWPS_LAYER_INBOUND_MAC_FRAME_ETHERNET                                           <br>
				FWPS_LAYER_OUTBOUND_MAC_FRAME_ETHERNET                                          <br>
				FWPS_LAYER_INBOUND_MAC_FRAME_NATIVE                                             <br>
				FWPS_LAYER_OUTBOUND_MAC_FRAME_NATIVE                                            <br>
				FWPS_LAYER_INGRESS_VSWITCH_ETHERNET                                             <br>
				FWPS_LAYER_EGRESS_VSWITCH_ETHERNET                                              <br>
																								<br>
			 TCP @ FWPM_LAYER_ALE_AUTH_CONNECT_V{4/6} has no NBL                                <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF544893.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID NTAPI ClassifyAdvancedPacketInjection(_In_ const FWPS_INCOMING_VALUES* pClassifyValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* pMetadata,
	_Inout_opt_ VOID* pNetBufferList,
	_In_opt_ const VOID* pClassifyContext,
	_In_ const FWPS_FILTER* pFilter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* pClassifyOut)
{
	NT_ASSERT(pClassifyValues);
	NT_ASSERT(pMetadata);
	NT_ASSERT(pFilter);
	NT_ASSERT(pClassifyOut);
	NT_ASSERT(pFilter->providerContext);
	NT_ASSERT(pFilter->providerContext->type == FWPM_GENERAL_CONTEXT);
	NT_ASSERT(pFilter->providerContext->dataBuffer);
	NT_ASSERT(pFilter->providerContext->dataBuffer->size == sizeof(PC_ADVANCED_PACKET_INJECTION_DATA));

#if(NTDDI_VERSION >= NTDDI_WIN8)

	NT_ASSERT(pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_MAC_FRAME_ETHERNET ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_MAC_FRAME_ETHERNET ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_MAC_FRAME_NATIVE ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_MAC_FRAME_NATIVE ||
		pClassifyValues->layerId == FWPS_LAYER_INGRESS_VSWITCH_ETHERNET ||
		pClassifyValues->layerId == FWPS_LAYER_EGRESS_VSWITCH_ETHERNET);

#else

	NT_ASSERT(pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6);

#endif /// (NTDDI_VERSION >= NTDDI_WIN8)


	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> ClassifyAdvancedPacketInjection() [Layer: %s][FilterID: %#I64x][Rights: %#x]",
		KrnlHlprFwpsLayerIDToString(pClassifyValues->layerId),
		pFilter->filterId,
		pClassifyOut->rights);

#if DBG

	PrvAdvancedPacketInjectionCountersIncrement(pClassifyValues,
		pMetadata,
		&g_apiTotalClassifies);

#endif /// DBG

	if (pClassifyOut->rights & FWPS_RIGHT_ACTION_WRITE)
	{
		/// Packets are not available for TCP @ ALE_AUTH_CONNECT, so skip over as there is nothing to inject
		if (pNetBufferList)
		{
			NTSTATUS        status = STATUS_SUCCESS;
			FWP_VALUE* pFlags = 0;
			INJECTION_DATA* pInjectionData = 0;

			pClassifyOut->actionType = FWP_ACTION_CONTINUE;

			pFlags = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
				&FWPM_CONDITION_FLAGS);
			if (pFlags &&
				pFlags->type == FWP_UINT32)
			{
				/// For IPsec interop, if  ALE classification is required, bypass the injection
				if (pFlags->uint32 & FWP_CONDITION_FLAG_IS_IPSEC_SECURED &&
					FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
						FWPS_METADATA_FIELD_ALE_CLASSIFY_REQUIRED))
					HLPR_BAIL;

				/// Inject the individual fragments, but not the fragment grouping of those fragments
				if (pFlags->uint32 & FWP_CONDITION_FLAG_IS_FRAGMENT_GROUP)
					HLPR_BAIL;
			}

#pragma warning(push)
#pragma warning(disable: 6014) /// pInjectionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

			status = KrnlHlprInjectionDataCreate(&pInjectionData,
				pClassifyValues,
				pMetadata,
				(NET_BUFFER_LIST*)pNetBufferList,
				pFilter);
			HLPR_BAIL_ON_FAILURE(status);

#pragma warning(pop)

			if (pInjectionData->injectionState != FWPS_PACKET_INJECTED_BY_SELF &&
				pInjectionData->injectionState != FWPS_PACKET_PREVIOUSLY_INJECTED_BY_SELF)
			{
				BOOLEAN                            performOutOfBand = TRUE;
				FWP_VALUE* pProtocolValue = 0;
				PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)pFilter->providerContext->dataBuffer->data;

				pClassifyOut->actionType = FWP_ACTION_BLOCK;
				pClassifyOut->flags |= FWPS_CLASSIFY_OUT_FLAG_ABSORB;
				pClassifyOut->rights ^= FWPS_RIGHT_ACTION_WRITE;

#if(NTDDI_VERSION >= NTDDI_WIN8)

				if (pMetadata->l2Flags & FWPS_L2_INCOMING_FLAG_RECLASSIFY_MULTI_DESTINATION)
					HLPR_BAIL;

#endif

				if (pFlags &&
					pFlags->type == FWP_UINT32 &&
					pFlags->uint32 & FWP_CONDITION_FLAG_IS_IPSEC_SECURED)
					pInjectionData->isIPsecSecured = TRUE;

				/// Override the default of performing Out of Band with the user's specified setting ...
				if (pData->performInline)
					performOutOfBand = FALSE;

				/// ... however, due to TCP's locking semantics, TCP can only be injected Out of Band at any transport layer or equivalent, ...
				pProtocolValue = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
					&FWPM_CONDITION_IP_PROTOCOL);
				if ((pProtocolValue &&
					pProtocolValue->uint8 == IPPROTO_TCP &&
					pClassifyValues->layerId > FWPS_LAYER_IPFORWARD_V6_DISCARD) ||
					pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V4 ||
					pClassifyValues->layerId == FWPS_LAYER_STREAM_PACKET_V6)
					performOutOfBand = TRUE;

				/// ... and inbound injection of loopback traffic requires us to use Out of Band modification as well due to address lookups.
				if (!performOutOfBand &&
					pFlags &&
					pFlags->type == FWP_UINT32 &&
					pFlags->uint32 & FWP_CONDITION_FLAG_IS_LOOPBACK &&
					pInjectionData->direction == FWP_DIRECTION_INBOUND)
				{
					FWP_VALUE* pLocalAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
						&FWPM_CONDITION_IP_LOCAL_ADDRESS);
					FWP_VALUE* pRemoteAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
						&FWPM_CONDITION_IP_REMOTE_ADDRESS);

					if ((pLocalAddress &&
						((pLocalAddress->type == FWP_UINT32 &&
							RtlCompareMemory(&(pLocalAddress->uint32),
								IPV4_LOOPBACK_ADDRESS,
								IPV4_ADDRESS_SIZE)) ||
							(pLocalAddress->type == FWP_BYTE_ARRAY16_TYPE &&
								pLocalAddress->byteArray16 &&
								RtlCompareMemory(&(pLocalAddress->byteArray16->byteArray16),
									IPV6_LOOPBACK_ADDRESS,
									IPV6_ADDRESS_SIZE)))) ||
						(pRemoteAddress &&
							((pRemoteAddress->type == FWP_UINT32 &&
								RtlCompareMemory(&(pRemoteAddress->uint32),
									IPV4_LOOPBACK_ADDRESS,
									IPV4_ADDRESS_SIZE)) ||
								(pRemoteAddress->type == FWP_BYTE_ARRAY16_TYPE &&
									pRemoteAddress->byteArray16 &&
									RtlCompareMemory(&(pRemoteAddress->byteArray16->byteArray16),
										IPV6_LOOPBACK_ADDRESS,
										IPV6_ADDRESS_SIZE)))))
						performOutOfBand = TRUE;
				}

				if (performOutOfBand)
					status = TriggerAdvancedPacketInjectionOutOfBand(pClassifyValues,
						pMetadata,
						pNetBufferList,
						pClassifyContext,
						pFilter,
						flowContext,
						pClassifyOut,
						pInjectionData,
						pData);
				else
					status = TriggerAdvancedPacketInjectionInline(pClassifyValues,
						pMetadata,
						pNetBufferList,
						pClassifyContext,
						pFilter,
						flowContext,
						pClassifyOut,
						&pInjectionData);
			}
			else
			{
				pClassifyOut->actionType = FWP_ACTION_PERMIT;

				KrnlHlprInjectionDataDestroy(&pInjectionData);

				DbgPrintEx(DPFLTR_IHVNETWORK_ID,
					DPFLTR_ERROR_LEVEL,
					"   -- Injection previously performed.\n");
			}

		HLPR_BAIL_LABEL:

			NT_ASSERT(status == STATUS_SUCCESS);

			if (status != STATUS_SUCCESS)
			{
				KrnlHlprInjectionDataDestroy(&pInjectionData);

				DbgPrintEx(DPFLTR_IHVNETWORK_ID,
					DPFLTR_ERROR_LEVEL,
					" !!!! ClassifyAdvancedPacketInjection() [status: %#x]\n",
					status);
			}
		}
		else
			pClassifyOut->actionType = FWP_ACTION_PERMIT;
	}

#if DBG

	PrvAdvancedPacketInjectionCountersIncrementTotalActionResults(pClassifyValues,
		pMetadata,
		pClassifyOut);

#endif /// DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- ClassifyAdvancedPacketInjection() [Layer: %s][FilterID: %#I64x][Action: %#x][Rights: %#x][Absorb: %s]\n",
		KrnlHlprFwpsLayerIDToString(pClassifyValues->layerId),
		pFilter->filterId,
		pClassifyOut->actionType,
		pClassifyOut->rights,
		(pClassifyOut->flags & FWPS_CLASSIFY_OUT_FLAG_ABSORB) ? "TRUE" : "FALSE");

	return;
}

#else

/**
 @classify_function="ClassifyAdvancedPacketInjection"

   Purpose:  Blocks the current NET_BUFFER_LIST and injects a new NBL back to the stack.        <br>
																								<br>
   Notes:    Applies to the following layers:                                                   <br>
				FWPS_LAYER_INBOUND_IPPACKET_V{4/6}                                              <br>
				FWPS_LAYER_OUTBOUND_IPPACKET_V{4/6}                                             <br>
				FWPS_LAYER_IPFORWARD_V{4/6}                                                     <br>
				FWPS_LAYER_INBOUND_TRANSPORT_V{4/6}                                             <br>
				FWPS_LAYER_OUTBOUND_TRANSPORT_V{4/6}                                            <br>
				FWPS_LAYER_DATAGRAM_DATA_V{4/6}                                                 <br>
				FWPS_LAYER_INBOUND_ICMP_ERROR_V{4/6}                                            <br>
				FWPS_LAYER_OUTBOUND_ICMP_ERROR_V{4/6}                                           <br>
				FWPS_LAYER_ALE_AUTH_CONNECT_V{4/6}                                              <br>
				FWPS_LAYER_ALE_FLOW_ESTABLISHED_V{4/6}                                          <br>
																								<br>
			 TCP @ FWPM_LAYER_ALE_AUTH_CONNECT_V{4/6} has no NBL                                <br>
																								<br>
   MSDN_Ref: HTTP://MSDN.Microsoft.com/En-US/Library/Windows/Hardware/FF544890.aspx             <br>
*/
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID NTAPI ClassifyAdvancedPacketInjection(_In_ const FWPS_INCOMING_VALUES* pClassifyValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* pMetadata,
	_Inout_opt_ VOID* pNetBufferList,
	_In_ const FWPS_FILTER* pFilter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* pClassifyOut)
{
	NT_ASSERT(pClassifyValues);
	NT_ASSERT(pMetadata);
	NT_ASSERT(pFilter);
	NT_ASSERT(pClassifyOut);
	NT_ASSERT(pFilter->providerContext);
	NT_ASSERT(pFilter->providerContext->type == FWPM_GENERAL_CONTEXT);
	NT_ASSERT(pFilter->providerContext->dataBuffer);
	NT_ASSERT(pFilter->providerContext->dataBuffer->size == sizeof(PC_ADVANCED_PACKET_INJECTION_DATA));
	NT_ASSERT(pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_IPPACKET_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_IPPACKET_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_IPFORWARD_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_DATAGRAM_DATA_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_INBOUND_ICMP_ERROR_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_OUTBOUND_ICMP_ERROR_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_AUTH_CONNECT_V6 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4 ||
		pClassifyValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V6);

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" ---> ClassifyAdvancedPacketInjection() [Layer: %s][FilterID: %#I64x][Rights: %#x]",
		KrnlHlprFwpsLayerIDToString(pClassifyValues->layerId),
		pFilter->filterId,
		pClassifyOut->rights);

#if DBG

	PrvAdvancedPacketInjectionCountersIncrement(pClassifyValues,
		pMetadata,
		&g_apiTotalClassifies);

#endif /// DBG

	if (pClassifyOut->rights & FWPS_RIGHT_ACTION_WRITE)
	{
		/// Packets are not available for TCP @ ALE_AUTH_CONNECT, so skip over as there is nothing to inject
		if (pNetBufferList)
		{
			NTSTATUS        status = STATUS_SUCCESS;
			FWP_VALUE* pFlags = 0;
			INJECTION_DATA* pInjectionData = 0;

			pClassifyOut->actionType = FWP_ACTION_CONTINUE;

			pFlags = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
				&FWPM_CONDITION_FLAGS);
			if (pFlags &&
				pFlags->type == FWP_UINT32)
			{
				/// For IPsec interop, if  ALE classification is required, bypass the injection
				if (pFlags->uint32 & FWP_CONDITION_FLAG_IS_IPSEC_SECURED &&

#if(NTDDI_VERSION >= NTDDI_WIN6SP1)

					FWPS_IS_METADATA_FIELD_PRESENT(pMetadata,
						FWPS_METADATA_FIELD_ALE_CLASSIFY_REQUIRED))

#else

					pFlags->uint32& FWP_CONDITION_FLAG_REQUIRES_ALE_CLASSIFY)

#endif // (NTDDI_VERSION >= NTDDI_WIN6SP1)

					HLPR_BAIL;

				/// Inject the individual fragments, but not the fragment grouping of those fragments
				if (pFlags->uint32 & FWP_CONDITION_FLAG_IS_FRAGMENT_GROUP)
					HLPR_BAIL;
			}

#pragma warning(push)
#pragma warning(disable: 6014) /// pInjectionData will be freed in completionFn using AdvancedPacketInjectionCompletionDataDestroy

			status = KrnlHlprInjectionDataCreate(&pInjectionData,
				pClassifyValues,
				pMetadata,
				(NET_BUFFER_LIST*)pNetBufferList,
				pFilter);
			HLPR_BAIL_ON_FAILURE(status);

#pragma warning(pop)

			if (pInjectionData->injectionState != FWPS_PACKET_INJECTED_BY_SELF &&
				pInjectionData->injectionState != FWPS_PACKET_PREVIOUSLY_INJECTED_BY_SELF)
			{
				BOOLEAN                            performOutOfBand = TRUE;
				FWP_VALUE* pProtocolValue = 0;
				PC_ADVANCED_PACKET_INJECTION_DATA* pData = (PC_ADVANCED_PACKET_INJECTION_DATA*)pFilter->providerContext->dataBuffer->data;

				pClassifyOut->actionType = FWP_ACTION_BLOCK;
				pClassifyOut->flags |= FWPS_CLASSIFY_OUT_FLAG_ABSORB;
				pClassifyOut->rights ^= FWPS_RIGHT_ACTION_WRITE;

				if (pFlags &&
					pFlags->type == FWP_UINT32 &&
					pFlags->uint32 & FWP_CONDITION_FLAG_IS_IPSEC_SECURED)
					pInjectionData->isIPsecSecured = TRUE;

				/// Override the default of performing Out of Band with the user's specified setting ...
				if (pData->performInline)
					performOutOfBand = FALSE;

				/// ... however, due to TCP's locking semantics, TCP can only be injected Out of Band at any transport layer or equivalent, ...
				pProtocolValue = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
					&FWPM_CONDITION_IP_PROTOCOL);
				if (pProtocolValue &&
					pProtocolValue->uint8 == IPPROTO_TCP &&
					pClassifyValues->layerId > FWPS_LAYER_IPFORWARD_V6_DISCARD)
					performOutOfBand = TRUE;

				/// ... and inbound injection of loopback traffic requires us to use Out of Band modification as well due to address lookups.
				if (!performOutOfBand &&
					pFlags &&
					pFlags->type == FWP_UINT32 &&
					pFlags->uint32 & FWP_CONDITION_FLAG_IS_LOOPBACK &&
					pInjectionData->direction == FWP_DIRECTION_INBOUND)
				{
					FWP_VALUE* pLocalAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
						&FWPM_CONDITION_IP_LOCAL_ADDRESS);
					FWP_VALUE* pRemoteAddress = KrnlHlprFwpValueGetFromFwpsIncomingValues(pClassifyValues,
						&FWPM_CONDITION_IP_REMOTE_ADDRESS);

					if ((pLocalAddress &&
						(pLocalAddress->type == FWP_UINT32 &&
							RtlCompareMemory(&(pLocalAddress->uint32),
								IPV4_LOOPBACK_ADDRESS,
								IPV4_ADDRESS_SIZE)) ||
						(pLocalAddress->type == FWP_BYTE_ARRAY16_TYPE &&
							RtlCompareMemory(&(pLocalAddress->byteArray16->byteArray16),
								IPV6_LOOPBACK_ADDRESS,
								IPV6_ADDRESS_SIZE))) ||
						(pRemoteAddress &&
							(pRemoteAddress->type == FWP_UINT32 &&
								RtlCompareMemory(&(pRemoteAddress->uint32),
									IPV4_LOOPBACK_ADDRESS,
									IPV4_ADDRESS_SIZE)) ||
							(pRemoteAddress->type == FWP_BYTE_ARRAY16_TYPE &&
								RtlCompareMemory(&(pRemoteAddress->byteArray16->byteArray16),
									IPV6_LOOPBACK_ADDRESS,
									IPV6_ADDRESS_SIZE))))
						performOutOfBand = TRUE;
				}

				if (performOutOfBand)
					status = TriggerAdvancedPacketInjectionOutOfBand(pClassifyValues,
						pMetadata,
						pNetBufferList,
						0,
						pFilter,
						flowContext,
						pClassifyOut,
						pInjectionData,
						pData);
				else
					status = TriggerAdvancedPacketInjectionInline(pClassifyValues,
						pMetadata,
						pNetBufferList,
						0,
						pFilter,
						flowContext,
						pClassifyOut,
						&pInjectionData);
			}
			else
			{
				pClassifyOut->actionType = FWP_ACTION_PERMIT;

				KrnlHlprInjectionDataDestroy(&pInjectionData);

				DbgPrintEx(DPFLTR_IHVNETWORK_ID,
					DPFLTR_ERROR_LEVEL,
					"   -- Injection previously performed.\n");
			}

		HLPR_BAIL_LABEL:

			NT_ASSERT(status == STATUS_SUCCESS);

			if (status != STATUS_SUCCESS)
			{
				KrnlHlprInjectionDataDestroy(&pInjectionData);

				DbgPrintEx(DPFLTR_IHVNETWORK_ID,
					DPFLTR_ERROR_LEVEL,
					" !!!! ClassifyAdvancedPacketInjection() [status: %#x]\n",
					status);
			}
		}
		else
			pClassifyOut->actionType = FWP_ACTION_PERMIT;
	}

#if DBG

	PrvAdvancedPacketInjectionCountersIncrementTotalActionResults(pClassifyValues,
		pMetadata,
		pClassifyOut);

#endif /// DBG

	DbgPrintEx(DPFLTR_IHVNETWORK_ID,
		DPFLTR_ERROR_LEVEL,
		" <--- ClassifyAdvancedPacketInjection() [Layer: %s][FilterID: %#I64x][Action: %#x][Rights: %#x][Absorb: %s]\n",
		KrnlHlprFwpsLayerIDToString(pClassifyValues->layerId),
		pFilter->filterId,
		pClassifyOut->actionType,
		pClassifyOut->rights,
		(pClassifyOut->flags & FWPS_CLASSIFY_OUT_FLAG_ABSORB) ? "TRUE" : "FALSE");

	return;
}

#endif // (NTDDI_VERSION >= NTDDI_WIN7)
