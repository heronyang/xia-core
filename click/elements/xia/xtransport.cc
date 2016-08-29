#include "../../userlevel/xia.pb.h"
#include <click/config.h>
#include <click/glue.hh>
#include <click/error.hh>
#include <click/error-syslog.hh>
#include <click/confparse.hh>
#include <click/packet_anno.hh>
#include <click/packet.hh>
#include <click/vector.hh>
#include "xtransport.hh"
#include <click/xiastreamheader.hh>
#include "xlog.hh"
#include "xdatagram.hh"
#include "xstream.hh"
#include <click/xiasecurity.hh>  // xs_getSHA1Hash()

/*
** FIXME:
** - check for memory leaks (slow leak caused by open/close of stream sockets)
** - see various FIXMEs in the code
** - replace copy_packet with uniqueify. not needed for migration in reliable transport
**	 still needed for datagram and cid??
*/

CLICK_DECLS

sock::sock(
	XTRANSPORT *trans,
	unsigned short apiport,
	uint32_t sockid,
	int type) : hstate(CREATE) {
	state = INACTIVE;
	reap = false;
	isBlocking = true;
	initialized = false;
	so_error = 0;
	so_debug = false;
	interface_id = -1;
	outgoing_iface = -1;
	polling = false;
	recv_pending = false;
	timer_on = false;
	hlim = HLIM_DEFAULT;
	full_src_dag = false;
	if (type == SOCK_STREAM)
		nxt_xport = CLICK_XIA_NXT_XSTREAM;
	else
		nxt_xport = CLICK_XIA_NXT_XDGRAM;
	backlog = 5;
	seq_num = 0;
	ack_num = 0;
	isAcceptedSocket = false;

	num_connect_tries = 0;
	num_retransmits = 0;
	num_close_tries = 0;

	pkt = NULL;
	send_buffer_size = DEFAULT_RECV_WIN_SIZE;
	send_base = 0;
	next_send_seqnum = 0;
	remote_recv_window = 0;
	recv_buffer_size = DEFAULT_RECV_WIN_SIZE;
	recv_base = 0;
	next_recv_seqnum = 0;
	dgram_buffer_start = 0;
	dgram_buffer_end = -1;
	recv_buffer_count = 0;
	pending_recv_msg = NULL;
	migrateack_waiting = false;
	last_migrate_ts = 0;
	num_migrate_tries = 0;
	migrate_pkt = NULL;
	recv_pending = false;
	port = apiport;
	transport = trans;
	sock_type = type;
	hlim = HLIM_DEFAULT;
	if (type == SOCK_STREAM)
		nxt = CLICK_XIA_NXT_XSTREAM;
	else
		nxt = CLICK_XIA_NXT_XDGRAM;
	refcount = 1;
	xcacheSock = false;
	id = sockid;
}

sock::sock() {
	port = 0;
	sock_type = 0;
	state = INACTIVE;
	isBlocking = true;
	initialized = false;
	so_error = 0;
	so_debug = false;
	interface_id = -1;
	outgoing_iface = -1;
	polling = false;
	recv_pending = false;
	timer_on = false;
	hlim = HLIM_DEFAULT;
	full_src_dag = false;
	nxt_xport = CLICK_XIA_NXT_DATA;
	backlog = 5;
	seq_num = 0;
	ack_num = 0;
	isAcceptedSocket = false;

	num_connect_tries = 0;
	num_retransmits = 0;
	num_close_tries = 0;

	pkt = NULL;
	send_buffer_size = DEFAULT_RECV_WIN_SIZE;
	send_base = 0;
	next_send_seqnum = 0;
	remote_recv_window = 0;
	recv_buffer_size = DEFAULT_RECV_WIN_SIZE;
	recv_base = 0;
	next_recv_seqnum = 0;
	dgram_buffer_start = 0;
	dgram_buffer_end = -1;
	recv_buffer_count = 0;
	pending_recv_msg = NULL;
	migrateack_waiting = false;
	last_migrate_ts = 0;
	num_migrate_tries = 0;
	migrate_pkt = NULL;
	recv_pending = false;
	refcount = 1;
	xcacheSock = false;
	id = 0;
}


XTRANSPORT::XTRANSPORT() : _timer(this)
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	cp_xid_type("SID", &_sid_type);	// FIXME: why isn't this a constant?
	_next_id = INITIAL_ID;
}



int XTRANSPORT::configure(Vector<String> &conf, ErrorHandler *errh)
{
	XIAPath local_addr;
	String hostname;
	XID local_4id;
	Element* routing_table_elem;
	bool is_dual_stack_router;
	_is_dual_stack_router = false;
    char xidString[50];

	/* Configure tcp relevant information */
	memset(&_tcpstat, 0, sizeof(_tcpstat));

	/* _empty_note.initialize(Notifier::EMPTY_NOTIFIER, router()); */

	_tcp_globals.tcp_keepidle 		= 120;
	_tcp_globals.tcp_keepintvl 		= 120;
	_tcp_globals.tcp_maxidle   		= 120;
	_tcp_globals.tcp_now 			= 0;
	_tcp_globals.so_recv_buffer_size = 0x10000;
	_tcp_globals.tcp_mssdflt		= 1024;
	_tcp_globals.tcp_rttdflt		= TCPTV_SRTTDFLT / PR_SLOWHZ;
	_tcp_globals.so_flags	   	 	= 0;
	_tcp_globals.so_idletime		= 0;
	_verbosity 						= VERB_ERRORS;

	bool so_flags_array[32];
	bool t_flags_array[10];
	memset(so_flags_array, 0, 32 * sizeof(bool));
	memset(t_flags_array, 0, 10 * sizeof(bool));

	if (cp_va_kparse(conf, this, errh,
						 "HOSTNAME", cpkP + cpkM, cpString, &hostname,
						 "LOCAL_4ID", cpkP + cpkM, cpXID, &local_4id,
						 "ROUTETABLENAME", cpkP + cpkM, cpElement, &routing_table_elem,
						 "NUM_PORTS", cpkP+cpkM, cpInteger, &_num_ports,
						 "IS_DUAL_STACK_ROUTER", 0, cpBool, &is_dual_stack_router,
						 "IDLETIME", 0, cpUnsigned, &(_tcp_globals.so_idletime),
						 "MAXSEG", 	0, cpUnsignedShort, &(_tcp_globals.tcp_mssdflt),
						 "RCVBUF", 	0, cpUnsigned, &(_tcp_globals.so_recv_buffer_size),
						 "WINDOW_SCALING", 0, cpUnsigned, &(_tcp_globals.window_scale),
						 "USE_TIMESTAMPS", 0, cpBool, &(_tcp_globals.use_timestamp),
						 "FIN_AFTER_TCP_FIN",  0, cpBool, &(so_flags_array[8]),
						 "FIN_AFTER_TCP_IDLE", 0, cpBool, &(so_flags_array[9]),
						 "FIN_AFTER_UDP_IDLE", 0, cpBool, &(so_flags_array[10]),
						 "VERBOSITY", 0, cpUnsigned, &(_verbosity), // not sure we need this
						 cpEnd) < 0)
	 return -1;

	for (int i = 0; i < 32; i++) {
		if (so_flags_array[i])
			_tcp_globals.so_flags |= ( 1 << i ) ;
	}
	_tcp_globals.so_idletime *= PR_SLOWHZ;
	if (_tcp_globals.window_scale > TCP_MAX_WINSHIFT)
		_tcp_globals.window_scale = TCP_MAX_WINSHIFT;


    /* TODO: How should we choose xcacheSid? */
    random_xid("SID", xidString);
    _xcache_sid.parse(xidString);

	_local_4id = local_4id;
	_hostname = hostname;
	click_chatter("XTRANSPORT: hostname is %s", _hostname.c_str());

	// IP:0.0.0.0 indicates NULL 4ID
	_null_4id.parse("IP:0.0.0.0");

	_is_dual_stack_router = is_dual_stack_router;

#if USERLEVEL
	_routeTable = dynamic_cast<XIAXIDRouteTable*>(routing_table_elem);
#else
	_routeTable = reinterpret_cast<XIAXIDRouteTable*>(routing_table_elem);
#endif

	return 0;
}



XTRANSPORT::~XTRANSPORT()
{
	// click is shutting down, so we can get away with being lazy here
	//Clear all hashtable entries
	XIDtoSock.clear();
	idToSock.clear();
	XIDtoPushPort.clear();
	XIDpairToSock.clear();
	XIDpairToConnectPending.clear();

	xcmp_listeners.clear();
	notify_listeners.clear();
}



int XTRANSPORT::initialize(ErrorHandler *)
{
	_timer.initialize(this);

	_fast_ticks = new Timer(this);
	_fast_ticks->initialize(this);
	_fast_ticks->schedule_after_msec(TCP_FAST_TICK_MS);

	_slow_ticks = new Timer(this);
	_slow_ticks->initialize(this);
	_slow_ticks->schedule_after_msec(TCP_SLOW_TICK_MS);

	_reaper = new Timer(this);
	_reaper->initialize(this);
	_reaper->schedule_after_msec(700);

	return 0;
}



void XTRANSPORT::push(int port, Packet *p_input)
{
	WritablePacket *p_in = p_input->uniqueify();

	switch(port) {
		case API_PORT:	// control packet from socket API
			ProcessAPIPacket(p_in);
			break;

		case BAD_PORT: //packet from ???
			ERROR("\n\nERROR: BAD INPUT PORT TO XTRANSPORT!!!\n\n");
			break;

		case NETWORK_PORT: //Packet from network layer
			ProcessNetworkPacket(p_in);
			p_in->kill();
			break;

		case XHCP_PORT:		//Packet with DHCP information
			ProcessXhcpPacket(p_in);
			p_in->kill();
			break;

		default:
			ERROR("packet from unknown port: %d\n", port);
			break;
	}
}



/*************************************************************
** HANDLER FUNCTIONS
*************************************************************/
// ?????????
enum {DAG, HID, RVDAG};

int XTRANSPORT::write_param(const String &conf, Element *e, void *vparam, ErrorHandler *errh)
{
	XTRANSPORT *f = static_cast<XTRANSPORT *>(e);

	switch (reinterpret_cast<intptr_t>(vparam)) {
	// DAG write handler is only for bootstrap of dag on routers
	case DAG:
	{
		XIAPath dag;
		if (cp_va_kparse(conf, f, errh,
						 "DAG", cpkP + cpkM, cpXIAPath, &dag,
						 cpEnd) < 0)
			return -1;
		f->_local_addr = dag;
		click_chatter("XTRANSPORT: DAG or Local addr is now %s", f->_local_addr.unparse().c_str());
		String local_addr_str = f->_local_addr.unparse().c_str();
		// If a dag was assigned, this is a router.
		// So assign the same DAG to all interfaces
		for(int i=0; i<f->_num_ports; i++) {
			if(!f->_interfaces.update(i, local_addr_str)) {
				click_chatter("ERROR: Updating dag: %s to iface: %d", local_addr_str.c_str(), i);
				return -1;
			}
		}
		break;

	}
	case RVDAG:
	{
		XIAPath rvdag;
		if (cp_va_kparse(conf, f, errh,
						 "RVDAG", cpkP + cpkM, cpXIAPath, &rvdag,
						 cpEnd) < 0)
			return -1;
		click_chatter("XTRANSPORT: RV DAG is now %s", f->_local_addr.unparse().c_str());
		String local_addr_str = f->_local_addr.unparse().c_str();
		// If a RV dag was assigned, this is a router.
		// So assign the same RV DAG to all interfaces
		for(int i=0; i<f->_num_ports; i++) {
			if(!f->_interfaces.update_rv_dag(i, local_addr_str)) {
				click_chatter("ERROR: Updating dag: %s to iface: %d", local_addr_str.c_str(), i);
				return -1;
			}
		}
		break;
	}
	case HID:
	{
		XID hid;
		if (cp_va_kparse(conf, f, errh,
					"HID", cpkP + cpkM, cpXID, &hid, cpEnd) < 0)
			return -1;
		f->_hid = hid;
		click_chatter("XTRANSPORT: HID assigned: %s", hid.unparse().c_str());
		// Apply the same HID to all interfaces with DAG *->HID
		String hid_dag = "RE " + hid.unparse();
		click_chatter("XTRANSPORT: Assigning address for all interfaces: %s", hid_dag.c_str());
		for(int i=0; i<f->_num_ports; i++) {
			if(!f->_interfaces.add(i, hid_dag)) {
				click_chatter("ERROR: Assigning dag: %s to iface: %d", hid_dag.c_str(), i);
				return -1;
			}
		}
		// Also use the HID dag as our local address until we have a network dag
		f->_local_addr.parse(hid_dag);
		break;
	}
	default:
		break;
	}
	return 0;
}



int XTRANSPORT::purge(const String & /*conf */, Element *e, void *thunk, ErrorHandler * /*errh */)
{
	XTRANSPORT* xt = static_cast<XTRANSPORT*>(e);
	int count = 0;
	int purge = thunk != 0;

	// If purge is true, kill all stream sockets
	// else kill those in TIME_WAIT state

	for (HashTable<uint32_t, sock*>::iterator it = xt->idToSock.begin(); it != xt->idToSock.end(); ++it) {
		sock *sk = it->second;

		if (sk->sock_type == SOCK_STREAM) {
			if (purge || sk->state == TIME_WAIT) {
				count++;
				WARN("purging %d\n", sk->port);
				xt->TeardownSocket(sk);
			}
		}
	}
	return count;
}



String XTRANSPORT::Netstat(Element *e, void *)
{
	String table;
	char line[512];
	XTRANSPORT* xt = static_cast<XTRANSPORT*>(e);

	for (HashTable<uint32_t, sock*>::iterator it = xt->idToSock.begin(); it != xt->idToSock.end(); ++it) {
		uint32_t _id = it->first;
		sock *sk = it->second;
		const char *type = SocketTypeStr(sk->sock_type);
		const char *state = "";
		const char *xid = "";
		XID source_xid;

		if (sk->sock_type == SOCK_STREAM) {
			state = StateStr(sk->state);
		}

		if ((sk->src_path.destination_node()) != static_cast<size_t>(-1)) {
			source_xid = sk->src_path.xid(sk->src_path.destination_node());
			xid = source_xid.unparse().c_str();
		}

		sprintf(line, "%d,%s,%s,%s,%d\n", _id, type, state, xid, sk->refcount);
		table += line;
	}

	return table;
}



void XTRANSPORT::add_handlers()
{
	//add_write_handler("local_addr", write_param, (void *)H_MOVE);
	add_write_handler("dag", write_param, (void *)DAG);
	add_write_handler("rvDAG", write_param, (void *)RVDAG);
	add_write_handler("hid", write_param, (void *)HID);
	add_write_handler("purge", purge, (void*)1);
	add_write_handler("flush", purge, 0);
	add_read_handler("netstat", Netstat, 0);
}



/*************************************************************
** HELPER FUNCTIONS
*************************************************************/
uint32_t XTRANSPORT::NewID()
{
	// since we are single threaded, there's currently no need
	// for a mutex here

	// values between 1 and INITIAL_ID can be freely used by
	// API control sockets to indicate the type of control message
	// if it helps make debugging easier. Since these sockets
	// aren't actual XIA sockets their id value is never used.

	// FIXME: account for old socket/id pairs when we wrap in case
	// they are still in use

	if (_next_id == 0) {
		_next_id = INITIAL_ID;
	}
	return ++_next_id;
}


Packet *XTRANSPORT::UDPIPPrep(Packet *p_in, int dport)
{
	p_in->set_dst_ip_anno(IPAddress("127.0.0.1"));
	SET_DST_PORT_ANNO(p_in, dport);

	return p_in;
}



char *XTRANSPORT::random_xid(const char *type, char *buf)
{
	// This is a stand-in function until we get certificate based names
	//
	// note: buf must be at least 45 characters long
	// (longer if the XID type gets longer than 3 characters)
	sprintf(buf, RANDOM_XID_FMT, type, click_random(0, 0xffffffff));

	return buf;
}



const char *XTRANSPORT::SocketTypeStr(int stype)
{
	const char *s = "???";
	switch (stype) {
		case SOCK_STREAM: s = "STREAM"; break;
		case SOCK_DGRAM:  s = "DGRAM";  break;
		case SOCK_RAW:	s = "RAW";	break;
	}
	return s;
}



const char *XTRANSPORT::StateStr(SocketState state)
{
	const char *s = "???";
	switch(state) {
		case INACTIVE:   s = "INACTIVE";   break;
		case LISTEN:	 s = "LISTEN";	 break;
		case SYN_RCVD:   s = "SYN_RCVD";   break;
		case SYN_SENT:   s = "SYN_SENT";   break;
		case CONNECTED:  s = "CONNECTED";  break;
		case FIN_WAIT1:  s = "FIN_WAIT1";  break;
		case FIN_WAIT2:  s = "FIN_WAIT2";  break;
		case TIME_WAIT:  s = "TIME_WAIT";  break;
		case CLOSING:	s = "CLOSING";	break;
		case CLOSE_WAIT: s = "CLOSE_WAIT"; break;
		case LAST_ACK:   s = "LAST_ACK";   break;
		case CLOSED:	 s = "CLOSED";	 break;
	}
	return s;
}



void XTRANSPORT::ChangeState(sock *sk, SocketState state)
{
	// FIXME: use the Xstream state instead of duplicating it in xtransport
	//INFO("socket %d changing state from %s to %s\n", sk->port, StateStr(sk->state), StateStr(state));
	sk->state = state;
}



bool XTRANSPORT::TeardownSocket(sock *sk)
{
	XID src_xid;
	XID dst_xid;
	bool have_src = 0;
	bool have_dst = 0;

	//INFO("Tearing down %s socket %d\n", SocketTypeStr(sk->sock_type), sk->get_id());

	CancelRetransmit(sk);

	if (sk->src_path.destination_node() != static_cast<size_t>(-1)) {
		src_xid = sk->src_path.xid(sk->src_path.destination_node());
		have_src = true;
	}
	if (sk->dst_path.destination_node() != static_cast<size_t>(-1)) {
		dst_xid = sk->dst_path.xid(sk->dst_path.destination_node());
		have_dst = true;
	}

	xcmp_listeners.remove(sk->get_id());

	if (sk->sock_type == SOCK_STREAM) {
		if (have_src && have_dst) {
			XIDpair xid_pair;
			xid_pair.set_src(src_xid);
			xid_pair.set_dst(dst_xid);

			XIDpairToConnectPending.erase(xid_pair);
			XIDpairToSock.erase(xid_pair);
		}

		// FIXME:delete these too
		//queue<sock*> pending_connection_buf;
		//queue<xia::XSocketMsg*> pendingAccepts;
		// MERGE - why is this commented out now?
		// for (int i = 0; i < sk->send_buffer_size; i++) {
		// 	if (sk->send_buffer[i] != NULL) {
		// 		sk->send_buffer[i]->kill();
		// 		sk->send_buffer[i] = NULL;
		// 	}
		// }
	}

	if (!sk->isAcceptedSocket) {
		// we only do this if the socket wasn't generateed due to an accept

		if (have_src) {
			//DBG("deleting route for %d %s\n", sk->port, src_xid.unparse().c_str());
			delRoute(src_xid);
			XIDtoSock.erase(src_xid);
		}
	}

	idToSock.erase(sk->get_id());

	delete sk;
	return true;
}


/*************************************************************
** RETRANSMIT CODE
*************************************************************/
void XTRANSPORT::ScheduleTimer(sock *sk, int delay)
{
	sk->timer_on = true;
	sk->expiry = Timestamp::now() + Timestamp::make_msec(delay);

	if (! _timer.scheduled() || _timer.expiry() >= sk->expiry)
		_timer.reschedule_at(sk->expiry);
}



void XTRANSPORT::CancelRetransmit(sock *sk)
{
	sk->num_connect_tries = 0;
	sk->num_retransmits = 0;
	sk->num_close_tries = 0;
	sk->timer_on = false;

	if (sk->pkt) {
		sk->pkt->kill();
		sk->pkt = NULL;
	}
}




bool XTRANSPORT::RetransmitMIGRATE(sock *sk, uint32_t id, Timestamp &now)
{
	UNUSED(sk);
	UNUSED(id);
	UNUSED(now);
	bool rc = false;
#if 0
	if (sk->num_migrate_tries <= MAX_RETRANSMIT_TRIES) {
		DBG("Socket %d MIGRATE retransmit\n", _sport);

		sk->timer_on = true;
		sk->migrateack_waiting = true;
		sk->expiry = now + Timestamp::make_msec(MIGRATEACK_DELAY);
		sk->num_migrate_tries++;

		WritablePacket *copy = copy_packet(sk->migrate_pkt, sk);
		output(NETWORK_PORT).push(copy);

	} else {
		WARN("Socket %d MIGRATE retransmit count exceeded\n", _sport);
		// FIXME: send RST?
		CancelRetransmit(sk);
		sk->migrateack_waiting = false;
		rc = true;
	}
#endif
	return rc;
}

void XTRANSPORT::run_timer(Timer *timer)
{
	ConnIterator i = XIDpairToSock.begin();
	ConnIterator j = XIDpairToConnectPending.begin();
	XStream *con = NULL;

	if (timer == _fast_ticks) {
		for (; i; i++) {
			if (i->second->get_type() == SOCK_STREAM &&
					!i->second->reap)
			{
				con = dynamic_cast<XStream *>(i->second);
				con->fasttimo();
			}
		}
		for (; j; j++) {
			if (j->second->get_type() == SOCK_STREAM &&
					!j->second->reap)
			{
				con = dynamic_cast<XStream *>(j->second);
				con->fasttimo();
			}
		}
		_fast_ticks->reschedule_after_msec(TCP_FAST_TICK_MS);
	} else if (timer == _slow_ticks) {
		for (; i; i++) {
			if (i->second->get_type() == SOCK_STREAM &&
					!i->second->reap)
			{
				con = dynamic_cast<XStream *>(i->second);
				con->slowtimo();
			}
		}
		for (; j; j++) {
			if (j->second->get_type() == SOCK_STREAM &&
					!j->second->reap)
			{
				con = dynamic_cast<XStream *>(j->second);
				con->slowtimo();
			}
		}
		_slow_ticks->reschedule_after_msec(TCP_SLOW_TICK_MS);
		(globals()->tcp_now)++;
	} else if (timer == _reaper) {
		for (; i; i++) {
			//INFO("This is %d, %d",i->second->id, i->second->reap);
			if (i->second->reap)
			{
				//INFO("Going to remove %d", i->second->id);
				TeardownSocket(i->second);
			}
		}
		_reaper->schedule_after_msec(700);
		// debug_output(VERB_TIMERS, "%u: XTRANSPORT::run_timer: unknown timer", tcp_now());
	}
	return;

}



/*************************************************************
** XHCP PACKET HANDLER
*************************************************************/
void XTRANSPORT::ProcessXhcpPacket(WritablePacket *p_in)
{
	WARN("IS THIS USED ANYMORE?\n");
	XIAHeader xiah(p_in->xia_header());
	String temp = _local_addr.unparse();
	Vector<String> ids;
	cp_spacevec(temp, ids);;
	if (ids.size() < 3) {
		String new_route((char *)xiah.payload());
		String new_local_addr = new_route + " " + ids[1];
		_local_addr.parse(new_local_addr);
	}
}



/*************************************************************
** NETWORK PACKET HANDLER
*************************************************************/
void XTRANSPORT::ProcessNetworkPacket(WritablePacket *p_in)
{
	XIAHeader xiah(p_in->xia_header());

	switch(xiah.nxt()) {
		case CLICK_XIA_NXT_XCMP:
			// pass the packet to all sockets that registered for XMCP packets
			ProcessXcmpPacket(p_in);
			return;

		case CLICK_XIA_NXT_XSTREAM:
			ProcessStreamPacket(p_in);
			return;

		case CLICK_XIA_NXT_XDGRAM:
			ProcessDatagramPacket(p_in);
			return;

		default:
			WARN("ProcessNetworkPacket: Unknown TransportType:%d\n", xiah.nxt());
			break;
	}
}


/*************************************************************
** DATAGRAM PACKET HANDLER
*************************************************************/
void XTRANSPORT::ProcessDatagramPacket(WritablePacket *p_in)
{
	XIAHeader xiah(p_in->xia_header());
	XIAPath dst_path = xiah.dst_path();
	XID _destination_xid(xiah.hdr()->node[xiah.last()].xid);

	sock *sk = XID2Sock(_destination_xid);  // This is to be updated for the XSOCK_STREAM type connections below

	if (!sk) {
		WARN("ProcessDatagramPacket: sk == NULL\n");
		return;
	}
	dynamic_cast<XDatagram *>(sk)->push(p_in);
}


/*************************************************************
** STREAMING TRANSPORT PACKET HANDLERS
*************************************************************/
void XTRANSPORT::ProcessStreamPacket(WritablePacket *p_in)
{
	// Is this packet arriving at a rendezvous server?
	if (HandleStreamRawPacket(p_in)) {
		// we handled it, no further processing is needed
		return;
	}
	// INFO("Inside ProcessStreamPacket");
	XIAHeader xiah(p_in->xia_header());
	XIAPath dst_path = xiah.dst_path();
	XIAPath src_path = xiah.src_path();

	// FIXME: why are these different??
	XID _destination_xid(xiah.hdr()->node[xiah.last()].xid);
	XID	_source_xid = src_path.xid(src_path.destination_node());

	XIDpair xid_pair;
	xid_pair.set_src(_destination_xid);
	xid_pair.set_dst(_source_xid);
	StreamHeader thdr(p_in);

	// printf("process stream: flags = %08x\n", thdr.flags());

	sock *handler;
	if ((handler = XIDpairToSock.get(xid_pair)) != NULL)
	{
		// INFO("We are in the normal case");

		// FIXME: is this ok?
		// switch over to use the DAG given to us by the other end
		handler->dst_path = src_path;

		((XStream *)handler) -> push(p_in);
	} else if ((handler = XIDpairToConnectPending.get(xid_pair)) != NULL)
	{
		// INFO("We are in the second case");

		// FIXME: is this ok?
		// switch over to use the DAG given to us by the other end
		handler->dst_path = src_path;

		((XStream *)handler) -> push(p_in);
	}
	else {
		if (thdr.flags() & XTH_SYN) {
			// unlike the other stream handlers, there is no pair yet, so use dest_xid to get port
			sock *sk = XID2Sock(_destination_xid);

			if (!sk) {
				// FIXME: we need to fix the state machine so this doesn't happen!
				WARN("sk == NULL\n");
				return;
			}

			// INFO("socket %d received SYN\n", sk->port);

			if (sk->state != LISTEN) {
				// we aren't marked to accept connecctions, drop it
				WARN("SYN received on a non-listening socket (port:%u), dropping...\n", sk->port);
				return;
			}

			if (sk->pending_connection_buf.size() >= sk->backlog) {
				// the backlog is full, we can't take it right now, drop it

				WARN("SYN received but backlog is full (port:%u), dropping...\n", sk->port);
				return;
			}

			// First, check if this request is already in the pending queue
			HashTable<XIDpair , struct sock*>::iterator it;
			it = XIDpairToConnectPending.find(xid_pair);

			if (it == XIDpairToConnectPending.end()) {
				// if this is new request, put it in the queue

				uint8_t hop_count = -1;
				// we have received a syn for CID,
				if (ntohl(_destination_xid.type()) == CLICK_XIA_XID_TYPE_CID) {
					hop_count = HLIM_DEFAULT - xiah.hlim();

				 	// but there is no DESTINED_FOR_LOCAL_HOST route for the destination CID, 
				 	// which means the CID is evicted, flatten the destination DAG.
				 	// 
					// we've received a request for a CID which usually contains a fallback
					// we need to strip out the direct path to the content and only use the
					// AD->HID->CID path.

					// Additionally, we may be a router which can service the request, so
					// don't just flatten the DAG, but make sure it points to us.
					// FIXME: are there any implications for multihoming here?
					// FIXME: can we do this without having to convert to strings?
					String str_local_addr = _local_addr.unparse_re();
					str_local_addr += " ";
//					str_local_addr += _xcache_sid.unparse();
//					str_local_addr += " ";
					str_local_addr += _destination_xid.unparse();

					sk->dst_path.parse_re(str_local_addr);
				}

				// send SYNACK to client
				// INFO("Socket %d Handling new SYN\n", sk->port);
				// Prepare new sock for this connection
				uint32_t new_id = NewID();
				XStream *new_sk = new XStream(this, 0, new_id); // just for now. This will be updated via Xaccept call
				new_sk->dst_path = src_path;
				new_sk->src_path = dst_path;
				new_sk->listening_sock = sk;
				new_sk->set_key(xid_pair);
				new_sk->set_hop_count(hop_count);
				XIDpairToConnectPending.set(xid_pair, new_sk);
				new_sk->push(p_in);
			}
		}
	}


}


bool XTRANSPORT::usingRendezvousDAG(XIAPath bound_dag, XIAPath pkt_dag)
{
	// If both DAGs match, then the pkt_dag did not come through rendezvous service
	if (bound_dag == pkt_dag) {
		return false;
	}
	INFO("DAG possibly modified by a rendezvous server");
	// Find local AD as of now
	XIAPath local_dag = local_addr();
	XID local_ad = local_dag.xid(local_dag.first_ad_node());
	XID bound_ad = bound_dag.xid(bound_dag.first_ad_node());
	XID packet_ad = pkt_dag.xid(pkt_dag.first_ad_node());
	// The local AD must be the same as that in the SYN packet
	if (packet_ad != local_ad) {
		INFO("AD was:%s but local AD is:%s", packet_ad.unparse().c_str(), local_ad.unparse().c_str());
		return false;
	}
	// difference between bound_dag and pkt_dag must be the bound_ad vs. local_ad
	if (bound_dag.compare_with_exception(pkt_dag, bound_ad, local_ad)) {
		ERROR("ERROR: Bound to network:%s", bound_ad.unparse().c_str());
		ERROR("ERROR: Current network:%s", local_ad.unparse().c_str());
		ERROR("ERROR: Wrong AD in packet pkt_dag:%s", pkt_dag.unparse().c_str());
		return false;
	}
	INFO("Allowing DAG different from bound dag");
	return true;
}



// FIXME: look into eliminating the common setup code in all of these function
// pass all as params?
void XTRANSPORT::ProcessMigratePacket(WritablePacket *p_in)
{
	UNUSED(p_in);
#if 0
	XIAHeader xiah(p_in->xia_header());

	XIAPath dst_path = xiah.dst_path();
	XIAPath src_path = xiah.src_path();

	XID _destination_xid(xiah.hdr()->node[xiah.last()].xid);
	XID	_source_xid = src_path.xid(src_path.destination_node());

	TransportHeader thdr(p_in);

	XIDpair xid_pair;
	xid_pair.set_src(_destination_xid);
	xid_pair.set_dst(_source_xid);

	sock *sk = XIDpairToSock.get(xid_pair);

	if (!sk) {
		WARN("ProcessMigratePacket: sk == NULL\n");
		return;
	}

	DBG("ProcessMigrate: %s from port %d at %ld.\n", _source_xid.unparse().c_str(), sk->port, Timestamp::now());

	// Verify the MIGRATE request and start using new DAG
	// No need to wait for an ACK because the operation is idempotent
	// 1. Retrieve payload (srcDAG, destDAG, seqnum) Signature, Pubkey
	const uint8_t *payload = thdr.payload();
	//int payload_len = xiah.plen() - thdr.hlen();
	const uint8_t *payloadptr = payload;
	String remote_DAG((char *)payloadptr, strlen((char *) payloadptr));
	payloadptr += strlen((char *)payloadptr) + 1;
	INFO("MIGRATE: remote DAG: %s", remote_DAG.c_str());
	String my_DAG((char *)payloadptr, strlen((char *) payloadptr));
	payloadptr += strlen((char *)payloadptr) + 1;
	INFO("MIGRATE: my DAG: %s", my_DAG.c_str());
	String timestamp((char *)payloadptr, strlen((char *) payloadptr));
	payloadptr += strlen((char *)payloadptr) + 1;
	INFO("MIGRATE: Timestamp: %s", timestamp.c_str());
	uint16_t siglen;
	memcpy(&siglen, payloadptr, sizeof(uint16_t));
	payloadptr += sizeof(uint16_t);
	INFO("MIGRATE: Signature length: %d", siglen);
	uint8_t *signature = (uint8_t *) calloc(siglen, 1);
	memcpy(signature, payloadptr, siglen);
	payloadptr += siglen;
	uint16_t pubkeylen;
	memcpy(&pubkeylen, payloadptr, sizeof(uint16_t));
	payloadptr += sizeof(uint16_t);
	INFO("MIGRATE: Pubkey length: %d", pubkeylen);
	char *pubkey = (char *) calloc(pubkeylen, 1);
	memcpy(pubkey, payloadptr, pubkeylen);
	INFO("MIGRATE: Pubkey:%s:", pubkey);
	payloadptr += pubkeylen;
	INFO("MIGRATE: pkt len: %d", payloadptr - payload);

	// 2. Verify hash of pubkey matches srcDAG destination node
	if (src_path.parse(remote_DAG) == false) {
		INFO("MIGRATE: ERROR parsing remote DAG:%s:", remote_DAG.c_str());
	}
	String src_SID_string = src_path.xid(src_path.destination_node()).unparse();
	const char *sourceSID = xs_XIDHash(src_SID_string.c_str());
	uint8_t pubkeyhash[SHA_DIGEST_LENGTH];
	char pubkeyhash_hexdigest[XIA_SHA_DIGEST_STR_LEN];
	xs_getPubkeyHash(pubkey, pubkeyhash, sizeof pubkeyhash);
	xs_hexDigest(pubkeyhash, sizeof pubkeyhash, pubkeyhash_hexdigest, sizeof pubkeyhash_hexdigest);
	if (strcmp(pubkeyhash_hexdigest, sourceSID) != 0) {
		INFO("ERROR: MIGRATE pubkey hash: %s SourceSID: %s", pubkeyhash_hexdigest, sourceSID);
	}
	INFO("MIGRATE: Source SID matches pubkey hash");

	// 3. Verify Signature using Pubkey
	size_t signed_datalen = remote_DAG.length() + my_DAG.length() + timestamp.length() + 3;
	if (!xs_isValidSignature(payload, signed_datalen, signature, siglen, pubkey, pubkeylen)) {
		INFO("ProcessNetworkPacket: ERROR: MIGRATE with invalid signature");
	}
	free(signature);
	free(pubkey);
	INFO("MIGRATE: Signature validated");

	// 4. Update socket state dst_path with srcDAG
	sk->dst_path = src_path;
	assert(sk->state == CONNECTED);

	// 5. Return MIGRATEACK to notify mobile host of change
	// Construct the payload - 'data'
	// For now (timestamp) signature, Pubkey
	uint8_t *data;
	uint8_t *dataptr;
	uint32_t maxdatalen;
	uint32_t datalen;
	char mypubkey[MAX_PUBKEY_SIZE];
	uint16_t mypubkeylen = MAX_PUBKEY_SIZE;

	INFO("MIGRATE: building MIGRATEACK");
	XID my_xid = sk->src_path.xid(sk->src_path.destination_node());
	INFO("MIGRATE: MIGRATEACK get pubkey for:%s:", my_xid.unparse().c_str());
	if (xs_getPubkey(my_xid.unparse().c_str(), mypubkey, &mypubkeylen)) {
		ERROR("ERROR: getting Pubkey for MIGRATEACK");
	}
	maxdatalen = remote_DAG.length() + 1 + timestamp.length() + 1 + MAX_SIGNATURE_SIZE + sizeof(uint16_t) + mypubkeylen;
	data = (uint8_t *) calloc(maxdatalen, 1);
	if (data == NULL) {
		ERROR("ERROR allocating memory for MIGRATEACK");
	}
	dataptr = data;

	// Insert the mobile host DAG whose migration has been accepted
	strcpy((char *)dataptr, remote_DAG.c_str());
	INFO("MIGRATE: MIGRATEACK remoteDAG: %s", (char *)dataptr);
	dataptr += remote_DAG.length() + 1; // null-terminated string

	// Insert timestamp into payload
	strcpy((char *)dataptr, timestamp.c_str());
	INFO("MIGRATE: MIGRATEACK timestamp: %s", (char *)dataptr);
	dataptr += timestamp.length() + 1; // null-terminated string

	// Sign(mobileDAG, Timestamp)
	uint8_t mysignature[MAX_SIGNATURE_SIZE];
	uint16_t mysiglen = MAX_SIGNATURE_SIZE;
	if (xs_sign(my_xid.unparse().c_str(), data, dataptr - data, mysignature, &mysiglen)) {
		ERROR("ERROR signing MIGRATEACK");
	}

	// Signature length
	memcpy(dataptr, &mysiglen, sizeof(uint16_t));
	INFO("MIGRATE: MIGRATEACK siglen: %d", mysiglen);
	dataptr += sizeof(uint16_t);

	// Signature
	memcpy(dataptr, mysignature, mysiglen);
	dataptr += mysiglen;

	// Public key length
	memcpy(dataptr, &mypubkeylen, sizeof(uint16_t));
	INFO("MIGRATE: MIGRATEACK pubkeylen: %d", mypubkeylen);
	dataptr += sizeof(uint16_t);

	// Public key
	memcpy(dataptr, mypubkey, mypubkeylen);
	INFO("MIGRATE: MIGRATEACK pubkey:%s:", dataptr);
	dataptr += mypubkeylen;

	// Total payload length
	datalen = dataptr - data;
	INFO("MIGRATE: MIGRATEACK len: %d", datalen);

	// Create a packet with the payload
	XIAHeaderEncap xiah_new;
	xiah_new.set_nxt(CLICK_XIA_NXT_TRN);
	xiah_new.set_last(LAST_NODE_DEFAULT);
	xiah_new.set_hlim(HLIM_DEFAULT);
	xiah_new.set_dst_path(src_path);
	xiah_new.set_src_path(dst_path);

	WritablePacket *just_payload_part = WritablePacket::make(256, data, datalen, 0);
	free(data);

	WritablePacket *p = NULL;

	xiah_new.set_plen(datalen);

	TransportHeaderEncap *thdr_new = TransportHeaderEncap::MakeMIGRATEACKHeader( 0, 0, 0, calc_recv_window(sk)); // #seq, #ack, length
	p = thdr_new->encap(just_payload_part);

	thdr_new->update();
	xiah_new.set_plen(datalen + thdr_new->hlen()); // XIA payload = transport header + transport-layer data

	p = xiah_new.encap(p, false);

	delete thdr_new;
	output(NETWORK_PORT).push(p);

	// 6. Notify the api of MIGRATE reception
	//   Do we need to? -Nitin
#endif
}



void XTRANSPORT::ProcessMigrateAck(WritablePacket *p_in)
{
	UNUSED(p_in);
/*
	XIAHeader xiah(p_in->xia_header());

	XIAPath dst_path = xiah.dst_path();
	XIAPath src_path = xiah.src_path();

	XID _destination_xid(xiah.hdr()->node[xiah.last()].xid);
	XID	_source_xid = src_path.xid(src_path.destination_node());

	TransportHeader thdr(p_in);

	XIDpair xid_pair;
	xid_pair.set_src(_destination_xid);
	xid_pair.set_dst(_source_xid);

	sock *sk = XIDpairToSock.get(xid_pair);
	if (!sk) {
		ERROR("sk == NULL\n");
		return;
	}
	unsigned short _dport = sk->port;

	if (sk->state != CONNECTED) {
		// This should never happen!
		ERROR("socket is not connected\n");
		return;
	}

	DBG("%s from port %d at %ld.\n", _source_xid.unparse().c_str(), _dport, Timestamp::now());

	// Verify the MIGRATEACK and start using new DAG
	// 1. Retrieve payload (migratedDAG, timestamp) signature, Pubkey
	const uint8_t *payload = thdr.payload();
	int payload_len = xiah.plen() - thdr.hlen();
	const uint8_t *payloadptr = payload;
	size_t signed_datalen;

	// Extract the migrated DAG that the fixed host accepted
	String migrated_DAG((char *)payloadptr, strlen((char *) payloadptr));
	payloadptr += strlen((char *)payloadptr) + 1;
	INFO("MIGRATEACK: migrated DAG: %s", migrated_DAG.c_str());

	// Extract the timestamp corresponding to the migration message that was sent
	// Helps handle a second migration before the first migration is completed
	String timestamp((char *)payloadptr, strlen((char *) payloadptr));
	payloadptr += strlen((char *)payloadptr) + 1;
	signed_datalen = payloadptr - payload;
	INFO("MIGRATEACK: timestamp: %s", timestamp.c_str());

	// Get the signature (migrated_DAG, timestamp)
	uint16_t siglen;
	memcpy(&siglen, payloadptr, sizeof(uint16_t));
	INFO("MIGRATEACK: siglen: %d", siglen);
	payloadptr += sizeof(uint16_t);
	uint8_t *signature = (uint8_t *) calloc(siglen, 1);
	memcpy(signature, payloadptr, siglen);
	payloadptr += siglen;

	// Get the Public key of the fixed host
	uint16_t pubkeylen;
	memcpy(&pubkeylen, payloadptr, sizeof(uint16_t));
	INFO("MIGRATEACK: pubkeylen: %d", pubkeylen);
	payloadptr += sizeof(uint16_t);
	char *pubkey = (char *) calloc(pubkeylen, 1);
	memcpy(pubkey, payloadptr, pubkeylen);
	INFO("MIGRATEACK: pubkey:%s:", pubkey);
	payloadptr += pubkeylen;
	if (payloadptr - payload != payload_len) {
		WARN("MIGRATEACK expected payload len=%d, got %d", payload_len, payloadptr - payload);
	}
	//assert(payloadptr-payload == payload_len);

	// 2. Verify hash of pubkey matches the fixed host's SID
	String fixed_SID_string = sk->dst_path.xid(sk->dst_path.destination_node()).unparse();
	uint8_t pubkeyhash[SHA_DIGEST_LENGTH];
	char pubkeyhash_hexdigest[XIA_SHA_DIGEST_STR_LEN];
	xs_getPubkeyHash(pubkey, pubkeyhash, sizeof pubkeyhash);
	xs_hexDigest(pubkeyhash, sizeof pubkeyhash, pubkeyhash_hexdigest, sizeof pubkeyhash_hexdigest);
	if (strcmp(pubkeyhash_hexdigest, xs_XIDHash(fixed_SID_string.c_str())) != 0) {
		ERROR("ERROR: MIGRATEACK: Mismatch: fixedSID: %s, pubkeyhash: %s", fixed_SID_string.c_str(), pubkeyhash_hexdigest);
	}
	INFO("Hash of pubkey matches fixed SID");

	// 3. Verify Signature using Pubkey
	if (!xs_isValidSignature(payload, signed_datalen, signature, siglen, pubkey, pubkeylen)) {
		ERROR("ERROR: MIGRATEACK: MIGRATE with invalid signature");
	}
	INFO("MIGRATEACK: Signature verified");
	free(signature);
	free(pubkey);

	// 4. Verify timestamp matches the latest migrate message
	if (strcmp(sk->last_migrate_ts.c_str(), timestamp.c_str()) != 0) {
		WARN("timestamp sent:%s:, migrateack has:%s:", sk->last_migrate_ts.c_str(), timestamp.c_str());
	}
	INFO("MIGRATEACK: verified timestamp");

	// 5. Update socket state src_path to use the new DAG
	// TODO: Verify migrated_DAG's destination node is the same as src_path's
	//	   before replacing with the migrated_DAG
	sk->src_path.parse(migrated_DAG);
	INFO("MIGRATEACK: updated sock state with newly acknowledged DAG");

	// 6. The data retransmissions can now resume
	sk->migrateack_waiting = false;
	sk->num_migrate_tries = 0;

	portToSock.set(_dport, sk);
	if (_dport != sk->port) {
		INFO("MIGRATEACK: ERROR _dport %d, sk->port %d", _dport, sk->port);
	}
*/
}



void XTRANSPORT::ProcessXcmpPacket(WritablePacket *p_in)
{
	list<uint32_t>::iterator i;

	for (i = xcmp_listeners.begin(); i != xcmp_listeners.end(); i++) {
		uint32_t id = *i;
		sock *sk = idToSock.get(id);

		if (sk && sk->sock_type == SOCK_RAW) {
			dynamic_cast<XDatagram *>(sk)->push(p_in);
		}
	}
}



void XTRANSPORT::MigrateFailure(sock *sk)
{
	if (sk->polling) {
		// tell API we are in trouble
		ProcessPollEvent(sk->port, POLLHUP);
	}

	if (sk->isBlocking && sk->recv_pending) {
		// The api is blocking on a recv, return an error
		ReturnResult(sk->port, sk->pending_recv_msg, -1, ESTALE);

		sk->recv_pending = false;
		delete sk->pending_recv_msg;
		sk->pending_recv_msg = NULL;
	}
}

sock *XTRANSPORT::XID2Sock(XID dest_xid)
{
	sock *sk = XIDtoSock.get(dest_xid);

	if (sk)
		return sk;

	if (ntohl(dest_xid.type()) == CLICK_XIA_XID_TYPE_CID) {
		DBG("Dest = CID, look up the sk for the xcache SID instead\n");
		// Packet destined to a CID. Handling it specially.
		// FIXME: This is hackish. Maybe give users the ability to
		// register their own rules?

		return XIDtoSock.get(_xcache_sid);
	}

	return NULL;
}


#if 0
void XTRANSPORT::ProcessSynPacket(WritablePacket *p_in)
{
	XIAHeader xiah(p_in->xia_header());

	XIAPath dst_path = xiah.dst_path();
	XIAPath src_path = xiah.src_path();

	XID _destination_xid(xiah.hdr()->node[xiah.last()].xid);
	XID	_source_xid = src_path.xid(src_path.destination_node());

	TransportHeader thdr(p_in);

	XIDpair xid_pair;
	xid_pair.set_src(_destination_xid);
	xid_pair.set_dst(_source_xid);

	// unlike the other stream handlers, there is no pair yet, so use dest_xid to get port
	sock *sk = XID2Sock(_destination_xid);

	if (!sk) {
		// FIXME: we need to fix the state machine so this doesn't happen!
		WARN("sk == NULL\n");
		return;
	}

	INFO("socket %d received SYN\n", sk->port);

	if (sk->state != LISTEN) {
		// we aren't marked to accept connecctions, drop it
		WARN("SYN received on a non-listening socket (port:%u), dropping...\n", sk->port);
		return;
	}

	if (sk->pending_connection_buf.size() >= sk->backlog) {
		// the backlog is full, we can't take it right now, drop it

		WARN("SYN received but backlog is full (port:%u), dropping...\n", sk->port);
		return;
	}

	// First, check if this request is already in the pending queue
	HashTable<XIDpair , struct sock*>::iterator it;
	it = XIDpairToConnectPending.find(xid_pair);

	if (it != XIDpairToConnectPending.end()) {
		// we've already seen it, ignore it
		INFO("Socket %d received duplicate SYN\n", sk->port);

		// FIXME: is this OK?
		it->second->num_retransmits = 0;
		return ;
	}

	// For a CID packet, modify dst_path variable
	dst_path = alterCIDDstPath(dst_path);

	// if this is new request, put it in the queue

	// send SYNACK to client
	INFO("Socket %d Handling new SYN\n", sk->port);

	XIAHeaderEncap xiah_new;
	xiah_new.set_nxt(CLICK_XIA_NXT_TRN);
	xiah_new.set_last(LAST_NODE_DEFAULT);
	xiah_new.set_hlim(HLIM_DEFAULT);
	xiah_new.set_dst_path(src_path);
	xiah_new.set_src_path(dst_path);

	WritablePacket *just_payload_part;
	int payloadLength;

	// FIXME: use SendControlPacket to send the SYNACK instead of building it by hand

	std::cout << "Xcachesock = " <<sk->xcacheSock << "\n";
	// FIXME: move to a separate function
	if(!sk->xcacheSock && usingRendezvousDAG(sk->src_path, dst_path)) {
		XID _destination_xid = dst_path.xid(dst_path.destination_node());
		INFO("Sending SYNACK with verification for RV DAG");
		// Destination DAG from the SYN packet
		String src_path_str = dst_path.unparse();

		// Current timestamp as nonce against replay attacks
		Timestamp now = Timestamp::now();
		double timestamp = strtod(now.unparse().c_str(), NULL);

		// Build the payload with DAG for this service and timestamp
		XIASecurityBuffer synackPayload(1024);
		synackPayload.pack(src_path_str.c_str(), src_path_str.length());
		synackPayload.pack((const char *)&timestamp, (uint16_t) sizeof timestamp);

		// Sign the synack payload
		char signature[MAX_SIGNATURE_SIZE];
		uint16_t signatureLength = MAX_SIGNATURE_SIZE;
		if(xs_sign(_destination_xid.unparse().c_str(), (unsigned char *)synackPayload.get_buffer(), synackPayload.size(), (unsigned char *)signature, &signatureLength)) {
			ERROR("ERROR unable to sign the SYNACK using private key for %s", _destination_xid.unparse().c_str());
			MigrateFailure(sk);
			return;
		}

		// Retrieve public key for this host
		char pubkey[MAX_PUBKEY_SIZE];
		uint16_t pubkeyLength = MAX_PUBKEY_SIZE;
		if(xs_getPubkey(_destination_xid.unparse().c_str(), pubkey, &pubkeyLength)) {
			ERROR("ERROR public key not found for %s", _destination_xid.unparse().c_str());
			MigrateFailure(sk);
			return;
		}

		// Prepare a signed payload (serviceDAG, timestamp)Signature, Pubkey
		XIASecurityBuffer signedPayload(2048);
		signedPayload.pack(synackPayload.get_buffer(), synackPayload.size());
		signedPayload.pack(signature, signatureLength);
		signedPayload.pack((char *)pubkey, pubkeyLength);

		just_payload_part = WritablePacket::make(256, (const void*)signedPayload.get_buffer(), signedPayload.size(), 1);
		payloadLength = signedPayload.size();
	} else {
		const char* dummy = "Connection_pending";
		just_payload_part = WritablePacket::make(256, dummy, strlen(dummy), 0);
		payloadLength = strlen(dummy);
	}

	WritablePacket *p = NULL;

	xiah_new.set_plen(payloadLength);

	TransportHeaderEncap *thdr_new = TransportHeaderEncap::MakeSYNACKHeader(0, 0, 0, calc_recv_window(sk)); // #seq, #ack, length, recv_wind
	p = thdr_new->encap(just_payload_part);

	thdr_new->update();
	xiah_new.set_plen(payloadLength + thdr_new->hlen()); // XIA payload = transport header + transport-layer data

	p = xiah_new.encap(p, false);
	delete thdr_new;

	// Prepare new sock for this connection
	sock *new_sk = new sock();
	ChangeState(new_sk, SYN_RCVD);
	new_sk->port = 0; // just for now. This will be updated via Xaccept call
	new_sk->sock_type = SOCK_STREAM;
	new_sk->dst_path = src_path;
	new_sk->src_path = dst_path;
	new_sk->isAcceptedSocket = true;
	new_sk->pkt = copy_packet(p, new_sk);
		int iface;
		// Interface card matching src_path to use during migration
		if((iface = IfaceFromSIDPath(new_sk->src_path)) != -1) {
			new_sk->outgoing_iface = iface;
		}
		new_sk->refcount = 1;

	memset(new_sk->send_buffer, 0, new_sk->send_buffer_size * sizeof(WritablePacket*));
	memset(new_sk->recv_buffer, 0, new_sk->recv_buffer_size * sizeof(WritablePacket*));

	ScheduleTimer(new_sk, ACK_DELAY);

	XIDpairToConnectPending.set(xid_pair, new_sk);

	output(NETWORK_PORT).push(p);
	INFO("Sent SYNACK from new socket\n");
}
#endif

int XTRANSPORT::HandleStreamRawPacket(WritablePacket *p_in)
{
	XIAHeader xiah(p_in->xia_header());

	XIAPath dst_path = xiah.dst_path();
	XIAPath src_path = xiah.src_path();

	XID _destination_xid(xiah.hdr()->node[xiah.last()].xid);
	XID	_source_xid = src_path.xid(src_path.destination_node());

	StreamHeader thdr(p_in);

	XIDpair xid_pair;
	xid_pair.set_src(_destination_xid);
	xid_pair.set_dst(_source_xid);

	sock *sk = XID2Sock(_destination_xid);

	if (!sk) {
		ERROR("sk == NULL\n");
		return 0;
	}

	// it's not a raw packet, so tell ProcessNetworkPacket to handle it
	if (sk->sock_type != SOCK_RAW) {
		return 0;
	}

	INFO("socket %d STATE:%s\n", sk->port, StateStr(sk->state));

	String src_path_str = src_path.unparse();
	String dst_path_str = dst_path.unparse();
	INFO("received stream packet on raw socket");
	INFO("src|%s|", src_path_str.c_str());
	INFO("dst|%s|", dst_path_str.c_str());
	INFO("len=%d", p_in->length());

	// RAW sockets are treated the same as datagram sockets
	dynamic_cast<XDatagram *>(sk)->push(p_in);

	return 1;
}


/*************************************************************
** API FACING FUNCTIONS
*************************************************************/
void XTRANSPORT::ProcessAPIPacket(WritablePacket *p_in)
{
	std::string p_buf;
	p_buf.assign((const char*)p_in->data(), (const char*)p_in->end_data());

	//protobuf message parsing
	xia::XSocketMsg xia_socket_msg;
	xia_socket_msg.ParseFromString(p_buf);
	uint32_t id = xia_socket_msg.id();

	//Extract the destination port
	unsigned short _sport = SRC_PORT_ANNO(p_in);

	// DBG("Push: Got packet from API id:%d", id);

	switch (xia_socket_msg.type()) {
	case xia::XSOCKET:
		Xsocket(_sport, id, &xia_socket_msg);
		break;
	case xia::XSETSOCKOPT:
		Xsetsockopt(_sport, id, &xia_socket_msg);
		break;
	case xia::XGETSOCKOPT:
		Xgetsockopt(_sport, id, &xia_socket_msg);
		break;
	case xia::XBIND:
		Xbind(_sport, id, &xia_socket_msg);
		break;
	case xia::XCLOSE:
		Xclose(_sport, id, &xia_socket_msg);
		break;
	case xia::XCONNECT:
		Xconnect(_sport, id, &xia_socket_msg);
		break;
	case xia::XLISTEN:
		Xlisten(_sport, id, &xia_socket_msg);
		break;
	case xia::XREADYTOACCEPT:
		XreadyToAccept(_sport, id, &xia_socket_msg);
		break;
	case xia::XACCEPT:
		Xaccept(_sport, id, &xia_socket_msg);
		break;
	case xia::XUPDATEDAG:
		Xupdatedag(_sport, id, &xia_socket_msg);
		break;
	case xia::XREADLOCALHOSTADDR:
		Xreadlocalhostaddr(_sport, id, &xia_socket_msg);
		break;
	case xia::XSETXCACHESID:
		XsetXcacheSid(_sport, id, &xia_socket_msg);
		break;
	case xia::XGETHOSTNAME:
		Xgethostname(_sport, id, &xia_socket_msg);
		break;
	case xia::XGETIFADDRS:
		Xgetifaddrs(_sport, id, &xia_socket_msg);
		break;
	case xia::XUPDATENAMESERVERDAG:
		Xupdatenameserverdag(_sport, id, &xia_socket_msg);
		break;
	case xia::XREADNAMESERVERDAG:
		Xreadnameserverdag(_sport, id, &xia_socket_msg);
		break;
	case xia::XISDUALSTACKROUTER:
		Xisdualstackrouter(_sport, id, &xia_socket_msg);
		break;
	case xia::XSEND:
		Xsend(_sport, id, &xia_socket_msg, p_in);
		break;
	case xia::XSENDTO:
		Xsendto(_sport, id, &xia_socket_msg, p_in);
		break;
	case xia::XRECV:
		Xrecv(_sport, id, &xia_socket_msg);
		break;
	case xia::XRECVFROM:
		Xrecvfrom(_sport, id, &xia_socket_msg);
		break;
	case xia::XGETPEERNAME:
		Xgetpeername(_sport, id, &xia_socket_msg);
		break;
	case xia::XGETSOCKNAME:
		Xgetsockname(_sport, id, &xia_socket_msg);
		break;
	case xia::XPOLL:
		Xpoll(_sport, id, &xia_socket_msg);
		break;
	case xia::XUPDATERV:
		Xupdaterv(_sport, id, &xia_socket_msg);
		break;
	case xia::XFORK:
		Xfork(_sport, id, &xia_socket_msg);
		break;
	case xia::XREPLAY:
		Xreplay(_sport, id, &xia_socket_msg);
		break;
	case xia::XNOTIFY:
		Xnotify(_sport, id, &xia_socket_msg);
		break;
	default:
		ERROR("ERROR: Unknown API request\n");
		break;
	}

	p_in->kill();
}



void XTRANSPORT::ReturnResult(unsigned short sport, xia::XSocketMsg *xia_socket_msg, int rc, int err)
{
	xia::X_Result_Msg *x_result = xia_socket_msg->mutable_x_result();
	x_result->set_return_code(rc);
	x_result->set_err_code(err);

	std::string p_buf;
	xia_socket_msg->SerializeToString(&p_buf);
	WritablePacket *reply = WritablePacket::make(256, p_buf.c_str(), p_buf.size(), 0);
	output(API_PORT).push(UDPIPPrep(reply, sport));
}



/*
** Handler for the Xsocket API call
*/
void XTRANSPORT::Xsocket(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	xia::X_Socket_Msg *x_socket_msg = xia_socket_msg->mutable_x_socket();
	int sock_type = x_socket_msg->type();

	// set the id we'll use for future communication with this socket
	assert(id == 0);
	id = NewID();
	xia_socket_msg->set_id(id);

	//DBG("create %s socket id=%d port=%d\n", SocketTypeStr(sock_type), id, _sport);
	sock *sk = NULL;
	switch (sock_type) {
	case SOCK_STREAM: {
		sk = new XStream(this, _sport, id);
		break;
	}
	case SOCK_RAW:
	case SOCK_DGRAM: {
		sk = new XDatagram(this, _sport, id, sock_type);
		break;
	}
	}

	// Map the id to sock
	idToSock.set(id, sk);

	// Return result to API
	ReturnResult(_sport, xia_socket_msg, 0);
}

/*
** Xsetsockopt API handler
*/
void XTRANSPORT::Xsetsockopt(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	xia::X_Setsockopt_Msg *x_sso_msg = xia_socket_msg->mutable_x_setsockopt();
	sock *sk = idToSock.get(id);

	if (!sk) {
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	switch (x_sso_msg->opt_type()) {
	case XOPT_HLIM:
	{
		int hl = x_sso_msg->int_opt();

		sk->hlim = hl;
	}
	break;

	case XOPT_NEXT_PROTO:
	{
		int nxt = x_sso_msg->int_opt();
		sk->nxt_xport = nxt;
		if (nxt == CLICK_XIA_NXT_XCMP)
			xcmp_listeners.push_back(id);
		else
			xcmp_listeners.remove(id);
	}
	break;

	case XOPT_BLOCK:
		sk->isBlocking = x_sso_msg->int_opt();
		break;

	case SO_DEBUG:
		sk->so_debug = x_sso_msg->int_opt();
		break;

		case SO_ERROR:
			sk->so_error = x_sso_msg->int_opt();
			break;

	default:
		// unsupported option
		break;
	}

	ReturnResult(_sport, xia_socket_msg);
}

/*
** Xgetsockopt API handler
*/
void XTRANSPORT::Xgetsockopt(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	xia::X_Getsockopt_Msg *x_sso_msg = xia_socket_msg->mutable_x_getsockopt();

	sock *sk = idToSock.get(id);

	if (!sk) {
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	switch (x_sso_msg->opt_type()) {
	case XOPT_HLIM:
		x_sso_msg->set_int_opt(sk->hlim);
		break;

	case XOPT_NEXT_PROTO:
		x_sso_msg->set_int_opt(sk->nxt_xport);
		break;

	case SO_ACCEPTCONN:
		x_sso_msg->set_int_opt(sk->state == LISTEN);
		break;

	case SO_DEBUG:
		x_sso_msg->set_int_opt(sk->so_debug);
		break;

	case SO_ERROR:
		x_sso_msg->set_int_opt(sk->so_error);
		sk->so_error = 0;
		break;

	case XOPT_ERROR_PEEK:
		// same as SO_ERROR, but doesn't reset the error code
		x_sso_msg->set_int_opt(sk->so_error);
		break;

	default:
		// unsupported option
		break;
	}

	ReturnResult(_sport, xia_socket_msg);
}

// Find interface corresponding to the SID DAG
// Returns interface number or -1 or error
int XTRANSPORT::IfaceFromSIDPath(XIAPath sidPath)
{
	XIAPath interface_dag = sidPath;
	//TODO: check dest node in sidPath is an SID
	if(interface_dag.remove_node(interface_dag.destination_node()) != true) {
		// TODO: XIAPath::remove_node always returns true. Remove this check
		click_chatter("Xtransport::IfaceFromSIDPath couldn't remove node");
		return -1;
	}
	return _interfaces.getIfaceID(interface_dag.unparse());
}

void XTRANSPORT::Xbind(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	int rc = 0, ec = 0;

	xia::X_Bind_Msg *x_bind_msg = xia_socket_msg->mutable_x_bind();

	String sdag_string(x_bind_msg->sdag().c_str(), x_bind_msg->sdag().size());

	//Set the source DAG in sock
	sock *sk = idToSock.get(id);

	if (!sk) {
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	if (sk->src_path.parse(sdag_string)) {
		sk->initialized = true;
		sk->port = _sport;

		//Check if binding to full DAG or just to SID only
		Vector<XIAPath::handle_t> xids = sk->src_path.next_nodes( sk->src_path.source_node() );
		XID front_xid = sk->src_path.xid( xids[0] );
		struct click_xia_xid head_xid = front_xid.xid();
		uint32_t head_xid_type = head_xid.type;
		if (head_xid_type == _sid_type) {
			sk->full_src_dag = false;
		} else {
			sk->full_src_dag = true;
		}

		XID	source_xid = sk->src_path.xid(sk->src_path.destination_node());
		//TODO: Add a check to see if XID is already being used

		// Try to determine the outgoing interface based on src_path
		// TODO: Should we do this only for stream sockets?
		int iface;
		if((iface = IfaceFromSIDPath(sk->src_path)) != -1) {
			sk->outgoing_iface = iface;
		}

		// Map the source XID to source port (for now, for either type of tranports)
		XIDtoSock.set(source_xid, sk);
		if(source_xid == _xcache_sid) {
			sk->xcacheSock = true;
		} else {
			sk->xcacheSock = false;
		}
		addRoute(source_xid);
		idToSock.set(xia_socket_msg->id(), sk);
		if (_sport != sk->port) {
			ERROR("ERROR _sport %d, sk->port %d", _sport, sk->port);
		}
	} else {
		rc = -1;
		ec = EADDRNOTAVAIL;
	}

	ReturnResult(_sport, xia_socket_msg, rc, ec);
}



void XTRANSPORT::Xfork(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(id);
	xia::X_Fork_Msg *msg = xia_socket_msg->mutable_x_fork();
	int count = msg->count();
	int increment = msg->increment() ? 1 : -1;

	// loop through list of ports and modify the ref counter
	for (int i = 0; i < count; i++) {
		uint32_t fid = msg->ids(i);

		sock *sk = idToSock.get(fid);
		if (sk) {
			sk->refcount += increment;
			//DBG("%s refcount for %d (%d)\n", (increment > 0 ? "incrementing" : "decrementing"), fid, sk->refcount);
			assert(sk->refcount > 0);
		}
	}

	ReturnResult(_sport, xia_socket_msg);
}



void XTRANSPORT::Xreplay(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(id);

	xia::X_Replay_Msg *msg = xia_socket_msg->mutable_x_replay();

	DBG("Received REPLAY packet\n");
//	xia_socket_msg->PrintDebugString();

	xia_socket_msg->set_type(msg->type());
	xia_socket_msg->set_sequence(msg->sequence());

	ReturnResult(_sport, xia_socket_msg);
}


void XTRANSPORT::Xnotify(unsigned short _sport, uint32_t id, xia::XSocketMsg * /*xia_socket_msg */)
{
	UNUSED(_sport);
	notify_listeners.push_back(id);

	// we just go away and wait for XchangeAD to be called which will trigger a response on this client socket
}


#if 0
// FIXME: This way of doing things is a bit hacky.
void XTRANSPORT::XbindPush(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	int rc = 0, ec = 0;

	xia::X_BindPush_Msg *x_bindpush_msg = xia_socket_msg->mutable_x_bindpush();

	String sdag_string(x_bindpush_msg->sdag().c_str(), x_bindpush_msg->sdag().size());

	DBG("\nbind requested to %s\n", sdag_string.c_str());

	//Set the source DAG in sock
	sock *sk = portToSock.get(_sport);
	if (sk->src_path.parse(sdag_string)) {
		ChangeState(sk, INACTIVE);
		sk->initialized = true;

		//Check if binding to full DAG or just to SID only
		Vector<XIAPath::handle_t> xids = sk->src_path.next_nodes( sk->src_path.source_node() );
		XID front_xid = sk->src_path.xid( xids[0] );

		struct click_xia_xid head_xid = front_xid.xid();
		uint32_t head_xid_type = head_xid.type;
		if (head_xid_type == _sid_type) {
			sk->full_src_dag = false;
		} else {
			sk->full_src_dag = true;
		}

		XID	source_xid = sk->src_path.xid(sk->src_path.destination_node());
		//TODO: Add a check to see if XID is already being used

		// Map the source XID to source port (for now, for either type of tranports)
		XIDtoPushPort.set(source_xid, _sport);
		addRoute(source_xid);

		portToSock.set(_sport, sk);
		if (_sport != sk->port) {
			ERROR("ERROR _sport %d, sk->port %d", _sport, sk->port);
		}

	} else {
		rc = -1;
		ec = EADDRNOTAVAIL;
		ERROR("ERROR: SOCKET PUSH BIND !!!\\n");
	}

	// (for Ack purpose) Reply with a packet with the destination port=source port
	ReturnResult(_sport, xia_socket_msg, rc, ec);
}
#endif


void XTRANSPORT::Xclose(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	int ref;
	bool should_delete;

	xia::X_Close_Msg *xcm = xia_socket_msg->mutable_x_close();
	// the passed id is for the control socket, not the one to close
	// so we get the id to close from the one given by the API
	uint32_t cid = xcm->id();

	sock *sk = idToSock.get(cid);
	bool teardown_now = true;

	//INFO("id=%d port=%d\n", cid, _sport);

	if (!sk) {
		// this shouldn't happen!
		ERROR("Invalid socket %d\n", id);
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	assert(sk->refcount != 0);

	should_delete = sk->isAcceptedSocket;
	ref = --sk->refcount;

	if (ref == 0) {

		//INFO("closing %d state=%s new refcount = %d\n", sk->port, StateStr(sk->state), ref);

		switch (sk -> get_type()) {
			case SOCK_STREAM:
				teardown_now = false;
				dynamic_cast<XStream *>(sk)->usrclosed();
				break;
			case SOCK_DGRAM:
				break;
		}

		if (teardown_now) {
			TeardownSocket(sk);
		}

	} else {
		// the app was forked and not everyone has closed the socket yet
		//INFO("decremented ref count on %d state=%s new refcount=%d\n", sk->port, StateStr(sk->state), ref);
	}

	xcm->set_refcount(ref);
	xcm->set_delkeys(should_delete);
	ReturnResult(_sport, xia_socket_msg);
}



void XTRANSPORT::Xconnect(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	xia::X_Connect_Msg *x_connect_msg = xia_socket_msg->mutable_x_connect();
	String dest(x_connect_msg->ddag().c_str());
	XIAPath dst_path;
	sock *sk = idToSock.get(id);

	dst_path.parse(dest);

	if (!sk) {
		ERROR("An Xstream socket using the same port # deleted our state\nwe can probably recover, but socket options will be lost\n");
		sk = new sock();

	}
	// else if (sk->state != INACTIVE) {
	// 	// a connect is already in progress
	// 	x_connect_msg->set_status(xia::X_Connect_Msg::XCONNECTING);
	// 	sk->so_error = EALREADY;
	// 	ReturnResult(_sport, xia_socket_msg, -1, EALREADY);
	// }
	if (sk ->get_type() == SOCK_STREAM) {
		XStream *tcp_conn = dynamic_cast<XStream *>(sk);
		if (tcp_conn -> tp->t_state == TCPS_SYN_SENT) {
			// a connect is already in progress
			x_connect_msg->set_status(X_Connect_Msg::XCONNECTING);
			ReturnResult(_sport, xia_socket_msg, -1, EALREADY);
		}

// FIXMEFIXMEFIXME: need id here?
		tcp_conn->dst_path = dst_path;
		tcp_conn->port = _sport;
		ChangeState(tcp_conn, SYN_SENT);
		tcp_conn->num_connect_tries++;

		String str_local_addr = _local_addr.unparse_re();

		// API sends a temporary DAG, if permanent not assigned by bind
		if (x_connect_msg->has_sdag()) {
			String sdag_string(x_connect_msg->sdag().c_str(), x_connect_msg->sdag().size());
			tcp_conn->src_path.parse(sdag_string);
		}

		// FIXME: is it possible for us not to have a source dag
		//   and if so, we should return an error
		assert(tcp_conn->src_path.is_valid());
		tcp_conn->set_nxt(LAST_NODE_DEFAULT);
		tcp_conn->set_last(LAST_NODE_DEFAULT);

		XID source_xid = tcp_conn->src_path.xid(tcp_conn->src_path.destination_node());
		XID destination_xid = tcp_conn->dst_path.xid(tcp_conn->dst_path.destination_node());
	// Determine outgoing interface based on src_path. Needed for migration
	int iface;
	if((iface = IfaceFromSIDPath(tcp_conn->src_path)) != -1) {
		tcp_conn->outgoing_iface = iface;
	} else {
		click_chatter("Xconnect: WARNING: could not determine outgoing iface for %s", tcp_conn->src_path.unparse().c_str());
	}


		XIDpair xid_pair;
		xid_pair.set_src(source_xid);
		xid_pair.set_dst(destination_xid);

		// Map the src & dst XID pair to source port()
		XIDpairToSock.set(xid_pair, tcp_conn);

		// Map the source XID to source port
		XIDtoSock.set(source_xid, tcp_conn);

		// Make us routable
		addRoute(source_xid);
		tcp_conn->usropen();
		ChangeState(tcp_conn, SYN_SENT);
		ChangeState(sk, SYN_SENT);

		// We return EINPROGRESS no matter what. If we're in non-blocking mode, the
		// API will pass EINPROGRESS on to the app. If we're in blocking mode, the API
		// will wait until it gets another message from xtransport notifying it that
		// the other end responded and the connection has been CONNECTED.
		x_connect_msg->set_status(xia::X_Connect_Msg::XCONNECTING);
		tcp_conn->so_error = EINPROGRESS;
		ReturnResult(_sport, xia_socket_msg, -1, EINPROGRESS);
	}
	// // Prepare SYN packet
	// const char *payload = "SYN";
	// SendControlPacket(TransportHeader::SYN, sk, payload, strlen(payload), dst_path, sk->src_path);
}



void XTRANSPORT::Xlisten(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	// we just want to mark the socket as listening and return right away.

	// FIXME: we should make sure we are already bound to a DAG
	// FIXME: make sure no one else is bound to this DAG

	xia::X_Listen_Msg *x_listen_msg = xia_socket_msg->mutable_x_listen();
	//INFO("Socket %d Xlisten\n", id);

	sock *sk = idToSock.get(id);
	if (!sk) {
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	if (sk->state == INACTIVE || sk->state == LISTEN) {
		ChangeState(sk, LISTEN);
		sk->backlog = x_listen_msg->backlog();
	} else {
		// FIXME: what is the correct error code to return
	}

	ReturnResult(_sport, xia_socket_msg);
}



void XTRANSPORT::XreadyToAccept(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	sock *sk = idToSock.get(id);

	if (!sk) {
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	if (!sk->pending_connection_buf.empty()) {
		// If there is already a pending connection, return true now
		//INFO("Pending connection is not empty\n");

		ReturnResult(_sport, xia_socket_msg);


	} else if (xia_socket_msg->blocking()) {
		// If not and we are blocking, add this request to the pendingAccept queue and wait

		//INFO("Pending connection is empty\n");

		// xia_socket_msg is on the stack; allocate a copy on the heap
		xia::XSocketMsg *xsm_cpy = new xia::XSocketMsg();
		xsm_cpy->CopyFrom(*xia_socket_msg);
		sk->pendingAccepts.push(xsm_cpy);

	} else {
		// socket is non-blocking and nothing is ready yet
		ReturnResult(_sport, xia_socket_msg, -1, EWOULDBLOCK);
	}
}



void XTRANSPORT::Xaccept(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	int rc = 0, ec = 0;

	// id is the *existing accept socket*

	unsigned short new_port = xia_socket_msg->x_accept().new_port();

	sock *sk = idToSock.get(id);
	if (!sk) {
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	//DBG("id %d, new_port %d seq:%d\n", id, new_port, xia_socket_msg->sequence());
	//DBG("p buf size = %d\n", sk->pending_connection_buf.size());
	//DBG("blocking = %d\n", sk->isBlocking);

	sk->hlim = HLIM_DEFAULT;
	sk->nxt_xport = CLICK_XIA_NXT_XSTREAM;

	if (!sk->pending_connection_buf.empty()) {
		sock *new_sk = sk->pending_connection_buf.front();
		uint32_t new_id = new_sk->get_id();

		//DBG("Get front element from and assign port number %d.", new_port);

		if (new_sk->state != CONNECTED) {
			ERROR("ERROR: sock from pending_connection_buf !isconnected\n");
		} else {
			//INFO("Socket on port %d is now connected\n", new_port);
		}
		new_sk->port = new_port;
		ChangeState(new_sk, CONNECTED);
		new_sk->isAcceptedSocket = true;

		idToSock.set(new_id, new_sk);

		sk->pending_connection_buf.pop();



// FIXME: does this block of code do anything??? I don't see the payload getting used
// I think it's all happening in the syn handling above?
/*
		WritablePacket *just_payload_part;
		int payloadLength;
		std::cout << "Xcachesock = " <<sk->xcacheSock << "\n";
		if((sk->xcacheSock == false) && usingRendezvousDAG(sk->src_path, new_sk->src_path)) {
			XID _destination_xid = new_sk->src_path.xid(new_sk->src_path.destination_node());
			INFO("Xaccept: Sending SYNACK with verification for RV DAG");
			// Destination DAG from the SYN packet
			String src_path_str = new_sk->src_path.unparse();

			// Current timestamp as nonce against replay attacks
			Timestamp now = Timestamp::now();
			double timestamp = strtod(now.unparse().c_str(), NULL);

			// Build the payload with DAG for this service and timestamp
			XIASecurityBuffer synackPayload(1024);
			synackPayload.pack(src_path_str.c_str(), src_path_str.length());
			synackPayload.pack((const char *)&timestamp, (uint16_t) sizeof timestamp);

			// Sign the synack payload
			char signature[MAX_SIGNATURE_SIZE];
			uint16_t signatureLength = MAX_SIGNATURE_SIZE;
			if (xs_sign(_destination_xid.unparse().c_str(), (unsigned char *)synackPayload.get_buffer(), synackPayload.size(), (unsigned char *)signature, &signatureLength)) {
				ERROR("ERROR unable to sign the SYNACK using private key for %s", _destination_xid.unparse().c_str());
				rc = -1;
				ec = ESTALE;
				goto Xaccept_done;
			}

			// Retrieve public key for this host
			char pubkey[MAX_PUBKEY_SIZE];
			uint16_t pubkeyLength = MAX_PUBKEY_SIZE;
			if (xs_getPubkey(_destination_xid.unparse().c_str(), pubkey, &pubkeyLength)) {
				ERROR("ERROR public key not found for %s", _destination_xid.unparse().c_str());
				rc = -1;
				ec = ESTALE;
				goto Xaccept_done;
			}

			// Prepare a signed payload (serviceDAG, timestamp)Signature, Pubkey
			XIASecurityBuffer signedPayload(2048);
			signedPayload.pack(synackPayload.get_buffer(), synackPayload.size());
			signedPayload.pack(signature, signatureLength);
			signedPayload.pack((char *)pubkey, pubkeyLength);

			just_payload_part = WritablePacket::make(256, (const void*)signedPayload.get_buffer(), signedPayload.size(), 1);
			payloadLength = signedPayload.size();
		}

		else {
			const char* dummy = "Connection_granted";
			just_payload_part = WritablePacket::make(256, dummy, strlen(dummy), 0);
			payloadLength = strlen(dummy);
		}
		*/

		// tell APP our id for future communications
		xia::X_Accept_Msg *x_accept_msg = xia_socket_msg->mutable_x_accept();
		x_accept_msg->set_new_id(new_sk->get_id());
		x_accept_msg->set_hop_count(new_sk->get_hop_count());
		// Get remote DAG to return to app
		x_accept_msg->set_remote_dag(new_sk->dst_path.unparse().c_str()); // remote endpoint is dest from our perspective

		if (xia_socket_msg->x_accept().has_sendmypath()) {
			DBG("Flag sendremotepath set for: %s\n", new_sk->src_path.unparse().c_str());
			x_accept_msg->set_self_dag(new_sk->src_path.unparse().c_str());
		}
	} else {
		// FIXME: THIS BETTER NOT HAPPEN!
		INFO("Got EWOULDBLOCK on a blocking accept!");
		rc = -1;
		ec = EWOULDBLOCK;
		goto Xaccept_done;
	}

Xaccept_done:
	ReturnResult(_sport, xia_socket_msg, rc, ec);
}



void XTRANSPORT::Xupdaterv(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(_sport);
	UNUSED(id);
	UNUSED(xia_socket_msg);
#if 0
	sock *sk = portToSock.get(_sport);

	// Retrieve rendezvous service DAG from user provided argument
	xia::X_Updaterv_Msg *x_updaterv_msg = xia_socket_msg->mutable_x_updaterv();
	int iface = x_updaterv_msg->interface();
	String rendezvousDAGstr(x_updaterv_msg->rvdag().c_str());

	// Update XIAInterfaceTable with new rendezvous control dag.
	// Note: If DAG is an empty string, it will be silently ignored.
	_interfaces.update_rv_control_dag(iface, rendezvousDAGstr);

	XIAInterface interface = _interfaces.getInterface(iface);
	// Return if we don't have a valid RV Control DAG to send a control msg to
	if(!interface.has_rv_control_dag()) {
		return;
	}

	// Send a control message to the rendezvous service to tell our location
	XIAPath rendezvousDAG;
	rendezvousDAG.parse(interface.rv_control_dag(), NULL);

	// HID of this host
	String ifaceHID = interface.hid().c_str();

	// Current local DAG for this host
	String ifaceDAGstr = interface.dag();

	// Current timestamp as nonce against replay attacks
	Timestamp now = Timestamp::now();
	double timestamp = strtod(now.unparse().c_str(), NULL);

	// Message going out to the rendezvous server
	INFO("RV DAG:%s", rendezvousDAGstr.c_str());
	INFO("for:%s", ifaceHID.c_str());
	INFO("at:%s", ifaceDAGstr.c_str());
	INFO("timestamp: %f", timestamp);

	// Prepare control message for rendezvous service
	XIASecurityBuffer controlMsg = XIASecurityBuffer(1024);
	controlMsg.pack(ifaceHID.c_str(), ifaceHID.length());
	controlMsg.pack(ifaceDAGstr.c_str(), ifaceDAGstr.length());
	controlMsg.pack((const char *)&timestamp, (uint16_t) sizeof timestamp);

	// Sign the control message
	char signature[MAX_SIGNATURE_SIZE];
	uint16_t signatureLength = MAX_SIGNATURE_SIZE;
	xs_sign(ifaceHID.c_str(), (unsigned char *)controlMsg.get_buffer(), controlMsg.size(), (unsigned char *)signature, &signatureLength);

	// Retrieve public key for this host
	char pubkey[MAX_PUBKEY_SIZE];
	uint16_t pubkeyLength = MAX_PUBKEY_SIZE;
	if(xs_getPubkey(ifaceHID.c_str(), pubkey, &pubkeyLength)) {
		ERROR("ERROR public key not found for %s", ifaceHID.c_str());
		return;
	}

	// Build signed message (hid, newDAG, timestamp)Signature, Pubkey
	XIASecurityBuffer signedMsg = XIASecurityBuffer(2048);
	signedMsg.pack(controlMsg.get_buffer(), controlMsg.size());
	signedMsg.pack(signature, signatureLength);
	signedMsg.pack((char *)pubkey, pubkeyLength);

	// Prepare XIP header
	XIAHeaderEncap xiah;
	xiah.set_last(LAST_NODE_DEFAULT);
	xiah.set_hlim(sk->hlim);
	xiah.set_dst_path(rendezvousDAG);
	xiah.set_src_path(sk->src_path);

	WritablePacket *just_payload_part = WritablePacket::make(256, (const void*)signedMsg.get_buffer(), signedMsg.size(), 1);

	WritablePacket *p = NULL;

	xiah.set_nxt(CLICK_XIA_NXT_TRN);
	xiah.set_plen(signedMsg.size());

	//Add XIA Transport headers
	TransportHeaderEncap *thdr = TransportHeaderEncap::MakeDGRAMHeader(0); // length
	p = thdr->encap(just_payload_part);

	thdr->update();
	xiah.set_plen(signedMsg.size() + thdr->hlen()); // XIA payload = transport header + transport-layer data

	p = xiah.encap(p, false);
	delete thdr;

	output(NETWORK_PORT).push(p);
#endif
}



// note this is only going to return status for a single socket in the poll response
// the only time we will return multiple sockets is when poll returns immediately
// TODO: is it worth changing this to possibly return more than one event?
void XTRANSPORT::ProcessPollEvent(uint32_t id, unsigned int flags_out)
{
	// loop thru all the polls that are registered looking for the socket associated with id
	for (HashTable<unsigned short, PollEvent>::iterator it = poll_events.begin(); it != poll_events.end(); it++) {
		uint32_t pollport = it->first;
		PollEvent pe = it->second;

		HashTable<uint32_t, unsigned int>::iterator sevent = pe.events.find(id);

		// socket isn't in this poll instance, keep looking
		if (sevent == pe.events.end()) {
			continue;
		}

		uint32_t pid = sevent->first;
		unsigned int mask = sevent->second;

		// if flags_out isn't an error and doesn't match the event mask keep looking
		if (!(flags_out & mask) && !(flags_out & (POLLHUP | POLLERR | POLLNVAL))) {
			continue;
		}

		xia::XSocketMsg xsm;
		xsm.set_id(21);
		xsm.set_type(xia::XPOLL);

		xia::X_Poll_Msg *msg = xsm.mutable_x_poll();
		msg->set_nfds(1);
		msg->set_type(xia::X_Poll_Msg::RESULT);

		xia::X_Poll_Msg::PollFD *pfd = msg->add_pfds();
		pfd->set_flags(flags_out);
		pfd->set_id(pid);

		//INFO("id=%d port=%d\n", pid, pollport);

		// do I need to set other flags in the return struct?
		ReturnResult(pollport, &xsm, 1, 0);

		// found the socket, decrement the polling count for all the sockets in the poll instance
		for (HashTable<uint32_t, unsigned int>::iterator pit = pe.events.begin(); pit != pe.events.end(); pit++) {
			pid = pit->first;

			sock *sk = idToSock.get(pid);
			if (sk)
				sk->polling--;
			// else should we pop it out of the list?
		}

		// get rid of this poll event
		poll_events.erase(it);
	}
}



void XTRANSPORT::CancelPollEvent(unsigned short _sport)
{
	PollEvent pe;
	unsigned short pollport;
	HashTable<unsigned short, PollEvent>::iterator it;

	// loop thru all the polls that are registered looking for the socket associated with _sport
	for (it = poll_events.begin(); it != poll_events.end(); it++) {
		pollport = it->first;
		pe = it->second;

		if (pollport == _sport)
			break;
		pollport = 0;
	}

	if (pollport == 0) {
		// we didn't find any events for this control socket
		// should we report error in this case?
		return;
	}

	// we have the poll event associated with this control socket

	// decrement the polling count for all the sockets in the poll instance
	for (HashTable<uint32_t, unsigned int>::iterator pit = pe.events.begin(); pit != pe.events.end(); pit++) {
		uint32_t pid = pit->first;

		sock *sk = idToSock.get(pid);
		if (sk) {
			sk->polling--;
		}
	}

	// get rid of this poll event
	poll_events.erase(it);
}



void XTRANSPORT::CreatePollEvent(unsigned short _sport, xia::X_Poll_Msg *msg)
{
	PollEvent pe;
	uint32_t nfds = msg->nfds();

	for (unsigned i = 0; i < nfds; i++) {
		const xia::X_Poll_Msg::PollFD& pfd = msg->pfds(i);

		uint32_t pid = pfd.id();
		unsigned flags = pfd.flags();

		// ignore ports that are set to 0, they are non-xia
		if (pid == 0)
			continue;

		// add the socket to this poll event
		pe.events.set(pid, flags);
		sock *sk = idToSock.get(pid);

		if (sk) {
			// let the socket know a poll is enabled on it
			sk->polling++;
		}
	}

	// register the poll event
	poll_events.set(_sport, pe);
}



void XTRANSPORT::Xpoll(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(id);
	xia::X_Poll_Msg *poll_in = xia_socket_msg->mutable_x_poll();

	if (poll_in->type() == xia::X_Poll_Msg::DOPOLL) {

		int actionable = 0;
		xia::XSocketMsg msg_out;
		msg_out.set_type(xia::XPOLL);
		msg_out.set_id(20);
		xia::X_Poll_Msg *poll_out = msg_out.mutable_x_poll();

		unsigned nfds = poll_in->nfds();

		for (unsigned i = 0; i < nfds; i++) {
			const xia::X_Poll_Msg::PollFD& pfd_in = poll_in->pfds(i);

			uint32_t pid = pfd_in.id();
			unsigned flags = pfd_in.flags();

			// skip over ignored sockets
			if ( pid == 0) {
				continue;
			}

			sock *sk = idToSock.get(pid);
			unsigned flags_out = 0;

			if (!sk) {
				// no socket state, we'll return an error right away
				flags_out = POLLNVAL;

			} else {
				// any accepts pending? never true unless a STREAM socket
				if (!sk->pending_connection_buf.empty()) {
					INFO("%d accepts are pending\n", sk->pending_connection_buf.size());
					flags_out |= POLLIN;
					if (flags & POLLOUT)
						flags |= POLLOUT;
				}

				// is there any read data?
				if (flags & POLLIN) {
					if (sk->sock_type == SOCK_STREAM) {
						XStream *st = dynamic_cast<XStream *>(sk);
						if (st->has_pullable_data()) {
							flags_out |= POLLIN;
						} else if (sk->state == CLOSE_WAIT) {
							// other end closed, app needs to know!
							flags_out |= POLLIN;
						}

					} else if (sk->sock_type == SOCK_DGRAM || sk->sock_type == SOCK_RAW) {
						if (sk->recv_buffer_count > 0) {
							flags_out |= POLLIN;
						}
					}
				}

				if (flags & POLLOUT) {
					// see if the socket is writable
					// FIXME should we be looking for anything else (send window, etc...)
					if (sk->sock_type == SOCK_STREAM) {
						if (sk->state == CONNECTED) {
							flags_out |= POLLOUT;
						}

					} else {
						flags_out |= POLLOUT;
					}
				}
			}

			if (flags_out) {
				// the socket can respond to the poll immediately
				xia::X_Poll_Msg::PollFD *pfd_out = poll_out->add_pfds();
				pfd_out->set_flags(flags_out);
				pfd_out->set_id(pid);

				actionable++;
			}
		}

		// we can return a result right away
		if (actionable) {
			poll_out->set_nfds(actionable);
			poll_out->set_type(xia::X_Poll_Msg::RESULT);

			ReturnResult(_sport, &msg_out, actionable, 0);

		} else {
			// we can't return a result yet
			CreatePollEvent(_sport, poll_in);
		}
	} else { // type == CANCEL
		// cancel the poll(s) on this control socket
		CancelPollEvent(_sport);
	}
}



void XTRANSPORT::Xupdatedag(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(id);
	//String str_local_addr = _local_addr.unparse();
	//size_t old_AD_start = str_local_addr.find_left("AD:");
	//size_t old_AD_end = str_local_addr.find_left(" ", old_AD_start);
	//String old_AD_str = str_local_addr.substring(old_AD_start, old_AD_end - old_AD_start);
	//String tmp = _local_addr.unparse();
	//Vector<String> ids;
	//cp_spacevec(tmp, ids);

	// Retrieve interface number and router's DAG from API message
	XIAPath router_dag;
	xia::X_Updatedag_Msg *x_updatedag_msg = xia_socket_msg->mutable_x_updatedag();
	int interface = x_updatedag_msg->interface();
	click_chatter("XTRANSPORT:Xupdatedag Updating interface: %d", interface);
	router_dag.parse(x_updatedag_msg->dag().c_str());
	click_chatter("XTRANSPORT:Xupdatedag Router addr: %s", router_dag.unparse().c_str());
	String IP4ID_str(x_updatedag_msg->ip4id().c_str());
	click_chatter("XTRANSPORT:Xupdatedag Router 4ID: %s (ignored unless in addr above)", IP4ID_str.c_str());

	String ad_table_str = _hostname + "/xrc/n/proc/rt_AD";
	String hid_table_str = _hostname + "/xrc/n/proc/rt_HID";
	String ip_table_str = _hostname + "/xrc/n/proc/rt_IP";
	String cmd;
	String cmdargs;
	String default_AD("-"), default_HID("-"), default_4ID("-");

	// Find the old dag that we will be replacing
	XIAPath old_dag;
	old_dag.parse(_interfaces.getDAG(interface));

	// Retrieve AD from old DAG for this interface
	XIAPath::handle_t h = old_dag.first_ad_node();
	if(h != INVALID_NODE_HANDLE) {
		XID old_ad = old_dag.xid(h);

		// Delete route for the old AD
		cmd = ad_table_str + ".remove";
		HandlerCall::call_write(cmd.c_str(), old_ad.unparse().c_str(), this);
	}

	// Retrieve RHID corresponding to the router for this interface
	if(_interfaces.has_rhid(interface)) {
		String old_rhid = _interfaces.getRHID(interface);

		// Delete route for this old RHID
		cmd = hid_table_str + ".remove";
		HandlerCall::call_write(cmd.c_str(), old_rhid.c_str(), this);
	}

	// Retrieve new AD from router_dag
	XID new_ad = router_dag.xid(router_dag.first_ad_node());

	// Retrieve new RHID from router_dag
	XID new_rhid = router_dag.xid(router_dag.find_intent_hid());

	// Add DESTINED_FOR_LOCALHOST route for new AD
	cmd = ad_table_str + ".add";
	cmdargs = new_ad.unparse() + " " + String(DESTINED_FOR_LOCALHOST);
	HandlerCall::call_write(cmd.c_str(), cmdargs.c_str(), this);

	click_chatter("XTRANSPORT::Xupdatedag Added localhost route for new AD");

	// If default interface, update default_AD, default_4ID, default_HID
	//    so they all point to new RHID
	if(interface == _interfaces.default_interface()) {

		// Set default AD to point to new RHID
		cmd = ad_table_str + ".set4";
		cmdargs = default_AD + ",0," + new_rhid.unparse() + "," + String(0xffff);
		click_chatter("XTRANSPORT: %s ...%s.", cmd.c_str(), cmdargs.c_str());
		HandlerCall::call_write(cmd.c_str(), cmdargs.c_str(), this);

		// Set default HID to point to new RHID
		cmd = hid_table_str + ".set4";
		cmdargs = default_HID + ",0," + new_rhid.unparse() + "," + String(0xffff);
		click_chatter("XTRANSPORT: %s ...%s.", cmd.c_str(), cmdargs.c_str());
		HandlerCall::call_write(cmd.c_str(), cmdargs.c_str(), this);

		// Set default AD to point to new RHID
		cmd = ip_table_str + ".set4";
		cmdargs = default_4ID + ",0," + new_rhid.unparse() + "," + String(0xffff);
		click_chatter("XTRANSPORT: %s ...%s.", cmd.c_str(), cmdargs.c_str());
		HandlerCall::call_write(cmd.c_str(), cmdargs.c_str(), this);
	}
	// Add new RHID route pointing to new RHID
	cmd = hid_table_str + ".add";
	cmdargs = new_rhid.unparse() + " " + new_rhid.unparse();
	HandlerCall::call_write(cmd.c_str(), cmdargs.c_str(), this);

	// Replace intent HID in router's DAG to form new_dag
	XIAPath new_dag = router_dag;
	if(!new_dag.replace_intent_hid(_hid)) {
		click_chatter("XTRANSPORT:Xupdatedag ERROR replacing intent HID in %s", new_dag.unparse().c_str());
		return;
	}

	// Update the XIAInterfaceTable with new dag
	_interfaces.update(interface, new_dag);
	NOTICE("Iface: %d: new addr: %s", interface, new_dag.unparse().c_str());

	// Add the corresponding rhid also
	_interfaces.update_rhid(interface, new_rhid.unparse());

	// Put the new DAG back into the xia_socket_msg to return to API
	x_updatedag_msg->set_dag(new_dag.unparse().c_str());


	// Build strings to reach all write handlers that need updating
	char linecardname[16];
	sprintf(linecardname, "xlc%d", interface);
	String linecardstr(linecardname);
	String linecardelem = _hostname + "/" + linecardstr;

	// XIAChallengeSource in XIALineCard being updated
	String xchal_handler_str = linecardelem + "/xchal.dag";
	click_chatter("XTRANSPORT:Xupdatedag notifying: %s", xchal_handler_str.c_str());
	HandlerCall::call_write(xchal_handler_str.c_str(), new_dag.unparse().c_str(), this);

	// XCMP in XIALineCard being updated
	String xlc_xcmp_handler_str = linecardelem + "/x.dag";
	click_chatter("XTRANSPORT::Xupdatedag notifying: %s", xlc_xcmp_handler_str.c_str());
	HandlerCall::call_write(xlc_xcmp_handler_str.c_str(), new_dag.unparse().c_str(), this);

	// If default interface:
	if(interface == _interfaces.default_interface()) {
		// XCMP in RoutingCore
		String xrc_str = _hostname + "/xrc/x.dag";
		HandlerCall::call_write(xrc_str.c_str(), new_dag.unparse().c_str(), this);
		// XCMP in RouteEngine
		String route_engine_str = _hostname + "/xrc/n/x.dag";
		HandlerCall::call_write(route_engine_str.c_str(), new_dag.unparse().c_str(), this);
		// All the XIAXIDRouteTable elements
		std::string routetables[5] = {"rt_AD", "rt_HID", "rt_SID", "rt_CID", "rt_IP"};
		for(int i=0; i<5; i++) {
			String route_table_str = _hostname + "/xrc/n/proc/" + routetables[i].c_str() + ".dag";
			click_chatter("XTRANSPORT:Xupdatedag notifying: %s", route_table_str.c_str());
			HandlerCall::call_write(route_table_str.c_str(), new_dag.unparse().c_str(), this);
		}
		// Update the _local_addr in XTRANSPORT
		_local_addr = new_dag;
		click_chatter("XTRANSPORT:Xupdatedag system addr changed to %s", _local_addr.unparse().c_str());
		NOTICE("new system address is - %s", _local_addr.unparse().c_str());
	}

#if 0
	// Inform all active stream connections about this change
	for (HashTable<uint32_t, sock*>::iterator iter = idToSock.begin(); iter != idToSock.end(); ++iter ) {
		uint32_t _migrateid = iter->first;
		sock *sk = idToSock.get(_migrateid);
		// Skip non-stream connections
		if (sk->sock_type != SOCK_STREAM) {
			continue;
		}
		// Skip inactive ports
		if (sk->state != CONNECTED) {
			INFO("skipping migration for non-connected port");
			INFO("src_path:%s:", sk->src_path.unparse().c_str());
			continue;
		}
		// Skip sockets not using the interface whose dag changed
		if(sk->outgoing_iface != interface) {
			INFO("skipping migration for unchanged interface");
			INFO("src_path:%s:", sk->src_path.unparse().c_str());
			INFO("out_iface:%d:change_iface:%d", sk->outgoing_iface, interface);
			continue;
		}

		// Retrieve SID from old src_path (unless available in sk already)
		XID sid = sk->src_path.xid(sk->src_path.destination_node());

		// Append SID to the new dag
		XIAPath new_sid_dag = new_dag;
		click_chatter("Xtransport::Xupdatedag new iface dag:%s", new_sid_dag.unparse().c_str());
		click_chatter("Xtransport::Xupdatedag appending:%s", sid.unparse().c_str());
		XIAPath::handle_t hid_handle = new_sid_dag.destination_node();
		XIAPath::handle_t sid_handle = new_sid_dag.add_node(sid);
		new_sid_dag.add_edge(hid_handle, sid_handle);
		new_sid_dag.set_destination_node(sid_handle);
		click_chatter("Xtransport::Xupdatedag new SID DAG:%s", new_sid_dag.unparse().c_str());
		INFO("Updating %s to %s in sk", sk->src_path.unparse().c_str(), new_sid_dag.unparse().c_str());
		sk->src_path = new_sid_dag;

		// Send MIGRATE message to each corresponding endpoint
		// src_DAG, dst_DAG, timestamp - Signed by private key
		// plus the public key (Should really exchange at SYN/SYNACK)
		uint8_t *payload;
		uint8_t *payloadptr;
		uint32_t maxpayloadlen;
		uint32_t payloadlen;
		String src_path = sk->src_path.unparse();
		String dst_path = sk->dst_path.unparse();
		INFO("MIGRATING %s - %s", src_path.c_str(), dst_path.c_str());
		int src_path_len = strlen(src_path.c_str()) + 1;
		int dst_path_len = strlen(dst_path.c_str()) + 1;
		Timestamp now = Timestamp::now();
		String timestamp = now.unparse();
		int timestamp_len = strlen(timestamp.c_str()) + 1;
		// Get the public key to include in packet
		char pubkey[MAX_PUBKEY_SIZE];
		uint16_t pubkeylen = MAX_PUBKEY_SIZE;
		XID src_xid = sk->src_path.xid(sk->src_path.destination_node());
		INFO("Retrieving pubkey for xid:%s:", src_xid.unparse().c_str());
		if (xs_getPubkey(src_xid.unparse().c_str(), pubkey, &pubkeylen)) {
			INFO("ERROR: Pubkey not found:%s:", src_xid.unparse().c_str());
			return;
		}
		INFO("Pubkey:%s:", pubkey);
		maxpayloadlen = src_path_len + dst_path_len + timestamp_len + sizeof(uint16_t) + MAX_SIGNATURE_SIZE + sizeof(uint16_t) + pubkeylen;
		payload = (uint8_t *)calloc(maxpayloadlen, 1);
		if (payload == NULL) {
			ERROR("ERROR: Cannot allocate memory for Migrate packet");
			return;
		}
		// Build the payload
		payloadptr = payload;
		// Source DAG with new AD
		memcpy(payloadptr, src_path.c_str(), src_path_len);
		INFO("MIGRATE Source DAG: %s", payloadptr);
		payloadptr += src_path_len;
		// Destination DAG
		memcpy(payloadptr, dst_path.c_str(), dst_path_len);
		INFO("MIGRATE Dest DAG: %s", payloadptr);
		payloadptr += dst_path_len;
		// Timestamp of this MIGRATE message
		memcpy(payloadptr, timestamp.c_str(), timestamp_len);
		INFO("MIGRATE Timestamp: %s", timestamp.c_str());
		payloadptr += timestamp_len;
		// Sign(SourceDAG, DestinationDAG, Timestamp)
		uint8_t signature[MAX_SIGNATURE_SIZE];
		uint16_t siglen = MAX_SIGNATURE_SIZE;
		if (xs_sign(src_xid.unparse().c_str(), payload, payloadptr - payload, signature, &siglen)) {
			ERROR("ERROR: Signing Migrate packet");
			return;
		}
		// Signature length
		memcpy(payloadptr, &siglen, sizeof(uint16_t));
		payloadptr += sizeof(uint16_t);
		// Signature
		memcpy(payloadptr, signature, siglen);
		payloadptr += siglen;
		// Public key length
		memcpy(payloadptr, &pubkeylen, sizeof(uint16_t));
		payloadptr += sizeof(uint16_t);
		// Public key of source SID
		memcpy(payloadptr, pubkey, pubkeylen);
		INFO("MIGRATE: Pubkey:%s: Length: %d", pubkey, pubkeylen);
		payloadptr += pubkeylen;
		// Total payload length
		payloadlen = payloadptr - payload;
		INFO("MIGRATE payload length: %d", payloadlen);

		//Add XIA headers
		XIAHeaderEncap xiah;
		xiah.set_nxt(CLICK_XIA_NXT_TRN);
		xiah.set_last(LAST_NODE_DEFAULT);
		xiah.set_hlim(sk->hlim);
		xiah.set_dst_path(sk->dst_path);
		xiah.set_src_path(sk->src_path);

		WritablePacket *just_payload_part = WritablePacket::make(256, payload, payloadlen, 0);
		free(payload);

		WritablePacket *p = NULL;

		TransportHeaderEncap *thdr = TransportHeaderEncap::MakeMIGRATEHeader( 0, 0, 0, calc_recv_window(sk)); // #seq, #ack, length

		p = thdr->encap(just_payload_part);

		thdr->update();
		xiah.set_plen(payloadlen + thdr->hlen()); // XIA payload = transport header + transport-layer data

		p = xiah.encap(p, false);

		delete thdr;

		// Store the migrate packet for potential retransmission
		sk->migrate_pkt = copy_packet(p, sk);
		sk->num_migrate_tries++;
		sk->last_migrate_ts = timestamp;

		// Set timer
		sk->migrateack_waiting = true;
		ScheduleTimer(sk, MIGRATEACK_DELAY);

		idToSock.set(_migrateid, sk);
		if (_migrateid != sk->id) {
			ERROR("ERROR _sport %d, sk->port %d", _migrateid, sk->id);
		}
		output(NETWORK_PORT).push(p);
	}
#endif

	ReturnResult(_sport, xia_socket_msg);


	// now also tell anyone who is waiting on a notification this happened
	list<uint32_t>::iterator i;
	xia::XSocketMsg xsm;
	xsm.set_type(xia::XNOTIFY);
	xsm.set_sequence(0);
	xsm.set_id(30);

	for (i = notify_listeners.begin(); i != notify_listeners.end(); i++) {
		uint32_t id = *i;
		sock *sk = idToSock.get(id);
		ReturnResult(sk->port, &xsm);
	}
	// get rid of them all now so we can start fresh
	notify_listeners.clear();
}



void XTRANSPORT::Xreadlocalhostaddr(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(id);

	// read the localhost AD and HID
	// TODO: Change from using _local_addr to _interfaces.default_dag()
	String dag_str = _local_addr.unparse();
	click_chatter("Xreadlocalhostaddr: dag: %s", dag_str.c_str());
	String IP4ID_str = _local_4id.unparse();
	click_chatter("Xreadlocalhostaddr: 4ID: %s", IP4ID_str.c_str());
	// return a packet containing localhost AD and HID
	xia::X_ReadLocalHostAddr_Msg *_msg = xia_socket_msg->mutable_x_readlocalhostaddr();
	_msg->set_dag(dag_str.c_str());
	_msg->set_ip4id(IP4ID_str.c_str());

	ReturnResult(_sport, xia_socket_msg);
}


void XTRANSPORT::XsetXcacheSid(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(id);
	UNUSED(_sport);
	xia::X_SetXcacheSid_Msg *_msg = xia_socket_msg->mutable_x_setxcachesid();

	_xcache_sid.parse(_msg->sid().c_str());
	// FIXME: this isn't returing a status
}



void XTRANSPORT::Xgethostname(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
    UNUSED(id);
	xia::X_GetHostName_Msg *_msg = xia_socket_msg->mutable_x_gethostname();
	_msg->set_hostname(_hostname.c_str());
	ReturnResult(_sport, xia_socket_msg);
}


void XTRANSPORT::_add_ifaddr(xia::X_GetIfAddrs_Msg *_msg, int interface)
{
	char iface_name[16];

	sprintf(iface_name, "iface%d", interface);
	xia::X_GetIfAddrs_Msg::IfAddr *ifaddr = _msg->add_ifaddrs();
	ifaddr->set_iface_name(iface_name);
	ifaddr->set_flags(0);
	ifaddr->set_src_addr_str(_interfaces.getDAG(interface).c_str());
	ifaddr->set_dst_addr_str(_interfaces.getDAG(interface).c_str());
}

void XTRANSPORT::Xgetifaddrs(unsigned short _sport, uint32_t id,
        xia::XSocketMsg *xia_socket_msg)
{
    UNUSED(id);
	xia::X_GetIfAddrs_Msg *_msg = xia_socket_msg->mutable_x_getifaddrs();

	// Add the default interface first
	_add_ifaddr(_msg, _interfaces.default_interface());
	// Read the _interfaces table
	for(int i=0; i<_interfaces.size(); i++) {
		if(i == _interfaces.default_interface()) {
			continue;
		}
		_add_ifaddr(_msg, i);
	}
	ReturnResult(_sport, xia_socket_msg);
}



void XTRANSPORT::Xupdatenameserverdag(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(id);

	xia::X_Updatenameserverdag_Msg *x_updatenameserverdag_msg = xia_socket_msg->mutable_x_updatenameserverdag();
	String ns_dag(x_updatenameserverdag_msg->dag().c_str());
	_nameserver_addr.parse(ns_dag);

	ReturnResult(_sport, xia_socket_msg);
}



void XTRANSPORT::Xreadnameserverdag(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(id);

	// read the nameserver DAG
	String ns_addr = _nameserver_addr.unparse();

	// return a packet containing the nameserver DAG
	xia::X_ReadNameServerDag_Msg *_msg = xia_socket_msg->mutable_x_readnameserverdag();
	_msg->set_dag(ns_addr.c_str());

	ReturnResult(_sport, xia_socket_msg);
}



void XTRANSPORT::Xisdualstackrouter(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	UNUSED(id);

	// return a packet indicating whether this node is an XIA-IPv4 dual-stack router
	xia::X_IsDualStackRouter_Msg *_msg = xia_socket_msg->mutable_x_isdualstackrouter();
	_msg->set_flag(_is_dual_stack_router);

	ReturnResult(_sport, xia_socket_msg);
}



void XTRANSPORT::Xgetpeername(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	sock *sk = idToSock.get(id);
	if (!sk) {
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	xia::X_GetPeername_Msg *_msg = xia_socket_msg->mutable_x_getpeername();
	_msg->set_dag(sk->dst_path.unparse().c_str());

	ReturnResult(_sport, xia_socket_msg);
}



void XTRANSPORT::Xgetsockname(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	sock *sk = idToSock.get(id);
	if (!sk) {
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}
	xia::X_GetSockname_Msg *_msg = xia_socket_msg->mutable_x_getsockname();
	_msg->set_dag(sk->src_path.unparse().c_str());

	ReturnResult(_sport, xia_socket_msg);
}



void XTRANSPORT::Xsend(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg, WritablePacket *p_in)
{
	int rc = 0, ec = 0;

	//Find socket state
	sock *sk = idToSock.get(id);

	// Make sure the socket state isn't null
	if (rc == 0 && !sk) {
		rc = -1;
		ec = EBADF;
	}

	xia::X_Send_Msg *x_send_msg = xia_socket_msg->mutable_x_send();
	int pktPayloadSize = x_send_msg->payload().size();

	//Find DAG info for that stream
	if (rc == 0 && sk->sock_type == SOCK_RAW) {
		char payload[65536];
		memcpy(payload, x_send_msg->payload().c_str(), pktPayloadSize);

		// FIXME: we should check to see that the packet isn't too big here
		//
		struct click_xia *xiah = reinterpret_cast<struct click_xia *>(payload);
		DBG("xiah->ver = %d", xiah->ver);
		DBG("xiah->nxt = %d", xiah->nxt);
		DBG("xiah->plen = %d", xiah->plen);
		DBG("xiah->hlim = %d", xiah->hlim);
		DBG("xiah->dnode = %d", xiah->dnode);
		DBG("xiah->snode = %d", xiah->snode);
		DBG("xiah->last = %d", xiah->last);
/*
		int total_nodes = xiah->dnode + xiah->snode;
		for(int i=0;i<total_nodes;i++) {
			uint8_t id[20];
			char hex_string[41];
			bzero(hex_string, 41);
			memcpy(id, xiah->node[i].xid.id, 20);
			for(int j=0;j<20;j++) {
				sprintf(&hex_string[2*j], "%02x", (unsigned int)id[j]);
			}
			char type[10];
			bzero(type, 10);
			switch (htonl(xiah->node[i].xid.type)) {
				case CLICK_XIA_XID_TYPE_AD:
					strcpy(type, "AD");
					break;
				case CLICK_XIA_XID_TYPE_HID:
					strcpy(type, "HID");
					break;
				case CLICK_XIA_XID_TYPE_SID:
					strcpy(type, "SID");
					break;
				case CLICK_XIA_XID_TYPE_CID:
					strcpy(type, "CID");
					break;
				case CLICK_XIA_XID_TYPE_IP:
					strcpy(type, "4ID");
					break;
				default:
					sprintf(type, "%d", xiah->node[i].xid.type);
			};
			INFO("%s:%s", type, hex_string);
		}
*/

		XIAHeader xiaheader(xiah);
		XIAHeaderEncap xiahencap(xiaheader);
		XIAPath dst_path = xiaheader.dst_path();
		INFO("Sending RAW packet to:%s:", dst_path.unparse().c_str());
		size_t headerlen = xiaheader.hdr_size();
		char *pktcontents = &payload[headerlen];
		int pktcontentslen = pktPayloadSize - headerlen;
		INFO("Packet size without XIP header:%d", pktcontentslen);

		WritablePacket *p = WritablePacket::make(p_in->headroom() + 1, (const void*)pktcontents, pktcontentslen, p_in->tailroom());
		p = xiahencap.encap(p, false);

		output(NETWORK_PORT).push(p);
		x_send_msg->clear_payload(); // clear payload before returning result
		ReturnResult(_sport, xia_socket_msg);
		return;
	}

	// Make sure socket is connected
	if (rc == 0 && sk->state != CONNECTED) {
		ERROR("ERROR: Socket on port %d was not connected!\n", sk->port);
		rc = -1;
		ec = ENOTCONN;
	}


	// If everything is OK so far, try sending
	if (rc == 0) {
		rc = pktPayloadSize;

		//Recalculate source path
		XID	source_xid = sk->src_path.xid(sk->src_path.destination_node());
		String str_local_addr = _local_addr.unparse_re() + " " + source_xid.unparse();
		//Make source DAG _local_addr:SID
		String dagstr = sk->src_path.unparse_re();

		//Client Mobility...
		if (dagstr.length() != 0 && dagstr != str_local_addr) {
			//Moved!
			// 1. Update 'sk->src_path'
			sk->src_path.parse_re(str_local_addr);
		}

		// Case of initial binding to only SID
		if (sk->full_src_dag == false) {
			sk->full_src_dag = true;
			String str_local_addr = _local_addr.unparse_re();
			XID front_xid = sk->src_path.xid(sk->src_path.destination_node());
			String xid_string = front_xid.unparse();
			str_local_addr = str_local_addr + " " + xid_string; //Make source DAG _local_addr:SID
			sk->src_path.parse_re(str_local_addr);
		}

		//Add XIA headers
		WritablePacket *payload = WritablePacket::make(p_in->headroom() + 1, (const void*)x_send_msg->payload().c_str(), pktPayloadSize, p_in->tailroom());

		if (sk -> get_type() == SOCK_STREAM) {	// why do we need this test, we should always be a stream socket here
			XStream *st = dynamic_cast<XStream *>(sk);

			int tcp_rc = st->usrsend(payload);
			//INFO("usrsend reurned %d\n", tcp_rc);
			if (tcp_rc != 0) {
				if (tcp_rc == EPIPE) {
					// the socket is closing
					ec = rc; // ? is this right???? if so should we signal from the API?/
					rc = -1;

				} else if (!xia_socket_msg->blocking()) {
					rc = -1;
					ec = EWOULDBLOCK;

				} else if (st->stage_data(payload, xia_socket_msg->sequence()) != false) {
					// transmit queue is full. Saving the data and not returning
					// until later when it has be added to the queue
					DBG("Transmit buffer full, saving and blocking\n");
					return;
				} else {
					// in theory this should never happen
					rc = -1;
					ec = ENOBUFS;
				}

			} else {
//				DBG("(%d) sent packet to %s, from %s\n", _sport, sk->dst_path.unparse_re().c_str(), sk->src_path.unparse_re().c_str());
			}

//			// FIXME: what is this for? we should already be in the table
//			portToSock.set(_sport, sk);
//			if (_sport != sk->port) {
//				ERROR("ERROR _sport %d, sk->port %d", _sport, sk->port);
//			}
		}
	}

	x_send_msg->clear_payload(); // clear payload before returning result
	ReturnResult(_sport, xia_socket_msg, rc, ec);
}



void XTRANSPORT::Xsendto(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg, WritablePacket *p_in)
{
	int rc = 0, ec = 0;

	xia::X_Sendto_Msg *x_sendto_msg = xia_socket_msg->mutable_x_sendto();

	String dest(x_sendto_msg->ddag().c_str());
	int pktPayloadSize = x_sendto_msg->payload().size();

	XIAPath dst_path;
	dst_path.parse(dest);

	//Find DAG info for this DGRAM
	sock *sk = idToSock.get(id);

	if (!sk) {
		//No local SID bound yet, so bind one
		sk = new sock();
	}

	if (sk->initialized == false) {
		sk->initialized = true;
		sk->full_src_dag = true;
		if (sk->port != _sport) {
			INFO("sk->port was %d setting to %d", sk->port, _sport);
		}
		sk->port = _sport;
		String str_local_addr = _local_addr.unparse_re();

		char xid_string[50];
		random_xid("SID", xid_string);
		str_local_addr = str_local_addr + " " + xid_string; //Make source DAG _local_addr:SID

		sk->src_path.parse_re(str_local_addr);

		XID	source_xid = sk->src_path.xid(sk->src_path.destination_node());

		XIDtoSock.set(source_xid, sk); //Maybe change the mapping to XID->sock?
		addRoute(source_xid);
	}

	// Case of initial binding to only SID
	if (sk->full_src_dag == false) {
		sk->full_src_dag = true;
		String str_local_addr = _local_addr.unparse_re();
		XID front_xid = sk->src_path.xid(sk->src_path.destination_node());
		String xid_string = front_xid.unparse();
		str_local_addr = str_local_addr + " " + xid_string; //Make source DAG _local_addr:SID
		sk->src_path.parse_re(str_local_addr);
	}


	if (sk->src_path.unparse_re().length() != 0) {
		//Recalculate source path
		XID	source_xid = sk->src_path.xid(sk->src_path.destination_node());
		String str_local_addr = _local_addr.unparse_re() + " " + source_xid.unparse(); //Make source DAG _local_addr:SID
		sk->src_path.parse(str_local_addr);
	}

	idToSock.set(id, sk);
	if (_sport != sk->port) {
		ERROR("ERROR _sport %d, sk->port %d", _sport, sk->port);
	}

	//Add XIA headers
	XIAHeaderEncap xiah;

	xiah.set_last(LAST_NODE_DEFAULT);
	xiah.set_hlim(sk->hlim);
	xiah.set_dst_path(dst_path);
	xiah.set_src_path(sk->src_path);

	WritablePacket *just_payload_part = WritablePacket::make(p_in->headroom() + 1, (const void*)x_sendto_msg->payload().c_str(), pktPayloadSize, p_in->tailroom());

	WritablePacket *p = NULL;

	if (sk->sock_type == SOCK_RAW) {
		xiah.set_nxt(sk->nxt_xport);

		xiah.set_plen(pktPayloadSize);
		p = xiah.encap(just_payload_part, false);

	} else {
		xiah.set_nxt(CLICK_XIA_NXT_XDGRAM);
		xiah.set_plen(pktPayloadSize);

		//Add XIA Transport headers
		DatagramHeaderEncap *dhdr = new DatagramHeaderEncap();
		p = dhdr->encap(just_payload_part);

		xiah.set_plen(pktPayloadSize + dhdr->hlen()); // XIA payload = transport header + transport-layer data

		p = xiah.encap(p, false);
		delete dhdr;
	}

	output(NETWORK_PORT).push(p);

	rc = pktPayloadSize;
	x_sendto_msg->clear_payload();
	ReturnResult(_sport, xia_socket_msg, rc, ec);
}



void XTRANSPORT::Xrecv(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	sock *sk = idToSock.get(id);

	if (!sk || sk->port != _sport) {
		ERROR("ERROR no socket associated with port %d", _sport);
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	if (sk && (sk->state == CONNECTED || sk->state == CLOSE_WAIT)) {

		if (sk -> sock_type == SOCK_STREAM)
		{
			((XStream*)sk)->read_from_recv_buf(xia_socket_msg);
		} else if (sk -> sock_type == SOCK_DGRAM)
		{
			((XDatagram*)sk)->read_from_recv_buf(xia_socket_msg);
		}

		if (xia_socket_msg->x_recv().bytes_returned() > 0) {
			// Return response to API
			//INFO("Read %d bytes", xia_socket_msg->x_recv().bytes_returned());
			ReturnResult(_sport, xia_socket_msg, xia_socket_msg->x_recv().bytes_returned());

		} else if (sk->state == CLOSE_WAIT) {
			// other end has closed, tell app there's nothing to read
			// what if other end is doing retransmits??
			ReturnResult(_sport, xia_socket_msg, 0);

		} else if (!xia_socket_msg->blocking()) {

			// we're not blocking and there's no data, so let API know immediately
			sk->recv_pending = false;
			ReturnResult(_sport, xia_socket_msg, -1, EWOULDBLOCK);

		} else {
			// rather than returning a response, wait until we get data
			sk->recv_pending = true; // when we get data next, send straight to app

			// xia_socket_msg is saved on the stack; allocate a copy on the heap
			xia::XSocketMsg *xsm_cpy = new xia::XSocketMsg();
			xsm_cpy->CopyFrom(*xia_socket_msg);
			sk->pending_recv_msg = xsm_cpy;
		}
	}
}


// FIXME: this is identical to Xrecv except for the protobuf type
// perhaps they should be combined
void XTRANSPORT::Xrecvfrom(unsigned short _sport, uint32_t id, xia::XSocketMsg *xia_socket_msg)
{
	sock *sk = idToSock.get(id);
	if (!sk) {
		ReturnResult(_sport, xia_socket_msg, -1, EBADF);
		return;
	}

	dynamic_cast<XDatagram *>(sk)->read_from_recv_buf(xia_socket_msg);

	if (xia_socket_msg->x_recvfrom().bytes_returned() > 0) {
		ReturnResult(_sport, xia_socket_msg, xia_socket_msg->x_recvfrom().bytes_returned());

	} else if (!xia_socket_msg->blocking()) {

		// we're not blocking and there's no data, so let API know immediately
		sk->recv_pending = false;
		ReturnResult(_sport, xia_socket_msg, -1, EWOULDBLOCK);

	} else {
		// rather than returning a response, wait until we get data
		sk->recv_pending = true; // when we get data next, send straight to app

		// xia_socket_msg is saved on the stack; allocate a copy on the heap
		xia::XSocketMsg *xsm_cpy = new xia::XSocketMsg();
		xsm_cpy->CopyFrom(*xia_socket_msg);
		sk->pending_recv_msg = xsm_cpy;
	}
}

CLICK_ENDDECLS

EXPORT_ELEMENT(XTRANSPORT)
ELEMENT_REQUIRES(userlevel)
ELEMENT_MT_SAFE(XTRANSPORT)
ELEMENT_LIBS(-lcrypto -lssl -lprotobuf)
