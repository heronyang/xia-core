#include "prefetch_utils.h"

int verbose = 1;

void say(const char *fmt, ...) 
{
	if (verbose) {
		va_list args;
		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
	}
}

void warn(const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
}

void die(int ecode, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
	fprintf(stdout, "Exiting\n");
	exit(ecode);
}

// create a semi-random alphanumeric string of the specified size
char *randomString(char *buf, int size) 
{
	int i;
	static const char *filler = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static int refresh = 1;
	int samples = strlen(filler);

	if (!(--refresh)) {
		// refresh rand every now and then so it doesn't degenerate too much
		// use a prime number to keep it interesting
		srand(time(NULL));
		refresh = 997;
	}
	for (i = 0; i < size - 1; i ++) {
		buf[i] = filler[rand() % samples];
	}
	buf[size - 1] = 0;

	return buf;
}

vector<string> strVector(char *strs) 
{
	char str_arr[strlen(strs)];
	strcpy(str_arr, strs);
	vector<string> strVec;
	char *str;
	strVec.push_back(strtok(str_arr, " "));	
	while ((str = strtok(NULL, " ")) != NULL) {
		strVec.push_back(str);
	}

	return strVec;
}

char *getPrefetchServiceName() 
{
	return string2char(string(PREFETCH_SERVER_NAME) + "." + getAD());
} 

char *getPrefetchManagerName() 
{
	return string2char(string(PREFETCH_MANAGER_NAME) + "." + getHID());
} 

char *getXftpName() 
{
	return string2char(string(FTP_NAME));
} 

char* string2char(string str) 
{
	char *cstr = new char[str.length() + 1];
	strcpy(cstr, str.c_str());	
	return cstr;
} 

long string2long(string str) 
{
	stringstream buffer(str);
	long var;
	buffer >> var;
	return var;
}

string execSystem(string cmd) 
{
	FILE* pipe = popen(string2char(cmd), "r");
	if (!pipe) return NULL;
  char buffer[128];
  string result = "";
  while (!feof(pipe)) {
		if (fgets(buffer, 128, pipe) != NULL)
			result += buffer;
	}
	pclose(pipe);

	if (result.empty()) return result; 	
	result.erase(result.end()-1, result.end()); // remove the newline character
	return result;
}

bool file_exists(const char * filename) 
{
	if (FILE * file = fopen(filename, "r")) {
		fclose(file);
		return true;
	}
	return false;
}

long now_msec() 
{
	struct timeval tv;
	if (gettimeofday(&tv, NULL) == 0)
		return ((tv.tv_sec * 1000000L + tv.tv_usec) / 1000);
	else
		return -1;
}

string getSSID()
{
	string ssid = execSystem(GETSSID_CMD);

	if (ssid.empty()) {
// cerr<<"No network\n";
		while (1) {
			usleep(SCAN_DELAY_MSEC * 1000);
			ssid = execSystem(GETSSID_CMD); 
			if (!ssid.empty()) {
// cerr<<"Network back\n";
				break;
			}
		}
	}
	return ssid;
}

string getAD() 
{
	int sock; 

	if ((sock = Xsocket(AF_XIA, SOCK_STREAM, 0)) < 0)
		die(-1, "Unable to create the listening socket\n");

	char ad[MAX_XID_SIZE], hid[MAX_XID_SIZE], ip[MAX_XID_SIZE];

	if (XreadLocalHostAddr(sock, ad, sizeof(ad), hid, sizeof(hid), ip, sizeof(ip)) < 0)
		die(-1, "Reading localhost address\n");

	Xclose(sock);

	return string(ad);
}

string getHID() 
{
	int sock; 

	if ((sock = Xsocket(AF_XIA, SOCK_STREAM, 0)) < 0)
		die(-1, "Unable to create the listening socket\n");

	char ad[MAX_XID_SIZE], hid[MAX_XID_SIZE], ip[MAX_XID_SIZE];

	if (XreadLocalHostAddr(sock, ad, sizeof(ad), hid, sizeof(hid), ip, sizeof(ip)) < 0)
		die(-1, "Reading localhost address\n");

	Xclose(sock);

	return string(hid);
}

void getNewAD(char *old_ad) 
{
	int sock; 

	if ((sock = Xsocket(AF_XIA, SOCK_STREAM, 0)) < 0)
		die(-1, "Unable to create the listening socket\n");
	char new_ad[MAX_XID_SIZE], hid[MAX_XID_SIZE], ip[MAX_XID_SIZE];

	while (1) {
		if (XreadLocalHostAddr(sock, new_ad, sizeof(new_ad), hid, sizeof(hid), ip, sizeof(ip)) < 0)
			die(-1, "Reading localhost address\n");
		if (strcmp(new_ad, old_ad) != 0) {
			cerr<<"AD changed!"<<endl;
			strcpy(old_ad, new_ad);
			Xclose(sock);
			return;			
		}
		usleep(SCAN_DELAY_MSEC * 1000);
	}
	return;
}

string netConnStatus(string lastSSID) 
{
	string currSSID = execSystem(GETSSID_CMD);

	if (currSSID.empty())	{
		return "empty";
	}
	else {
		if (currSSID == lastSSID) {
			return "same";
		}
		else {
			return currSSID;
		}
	}
}

int getReply(int sock, const char *cmd, char *reply, sockaddr_x *sa, int timeout, int tries) 
{
	int sent, received, rc;

	for (int i = 0; i < tries; i++) {
		string currSSID = execSystem(GETSSID_CMD);
		if (currSSID.empty())
			return -2;
		else {
			if ((sent = Xsendto(sock, cmd, strlen(cmd), 0, (sockaddr*)sa, sizeof(sockaddr_x))) < 0) {
				die(-4, "Send error %d on socket %d\n", errno, sock);
			}

			say("Xsock %4d sent %d bytes\n", sock, sent);

			struct pollfd pfds[2];
			pfds[0].fd = sock;
			pfds[0].events = POLLIN;
			if ((rc = Xpoll(pfds, 1, timeout)) <= 0) {
				say("Will poll next time\n");
				//die(-5, "Poll returned %d\n", rc);
			}

			memset(reply, 0, strlen(reply));
			if ((received = Xrecvfrom(sock, reply, strlen(reply), 0, NULL, NULL)) < 0)
				die(-5, "Receive error %d on socket %d\n", errno, sock);
			else {
				say("Xsock %4d received %d bytes\n", sock, received);
				return received;
			}
		}
	}
	return -3; 
}

int sendStreamCmd(int sock, const char *cmd) 
{
	warn("Sending Command: %s \n", cmd);
	int n;
	if ((n = Xsend(sock, cmd,  strlen(cmd), 0)) < 0) {
		Xclose(sock);
		die(-1, "Unable to communicate\n");
	}
	return n;
}

int sayHello(int sock, const char *helloMsg) 
{
	int m = sendStreamCmd(sock, helloMsg); 
	return m;
}

int hearHello(int sock) 
{ 
	char command[XIA_MAXBUF];
	memset(command, '\0', strlen(command));
	int n;
	if ((n = Xrecv(sock, command, RECV_BUF_SIZE, 0))  < 0) {
		warn("socket error while waiting for data, closing connection\n");
	}
	printf("%s\n", command);	
	return n;
	/*
	if (strncmp(command, "get", 3) == 0) {
		sscanf(command, "get %s %d %d", fin, &start, &end);
		printf("get %s %d %d\n", fin, start, end);
	if (strncmp(command, "Hello from context client", 25) == 0) {
		say("Received hello from context client\n");
		char* hello = "Hello from prefetch client";
		sendCmd(prefetchProfileSock, hello);
	}	
	*/
}

int XgetRemoteAddr(int sock, char *ad, char *hid, char *sid) 
{
	sockaddr_x dag;
	socklen_t daglen = sizeof(dag);
	char sdag[1024];

	if (Xgetpeername(sock, (struct sockaddr*)&dag, &daglen) < 0)
		die(-1, "unable to locate: %s\n", sock);

	Graph g(&dag);
	strncpy(sdag, g.dag_string().c_str(), sizeof(sdag));
	// say("sdag = %s\n",sdag);	
	char *ads = strstr(sdag, "AD:");	// first occurrence
	char *hids = strstr(sdag, "HID:");	
	char *sids = strstr(sdag, "SID:"); 
	if (sscanf(ads, "%s", ad) < 1 || strncmp(ad, "AD:", 3) != 0) {
		die(-1, "Unable to extract AD.");
	}
	if (sscanf(hids, "%s", hid) < 1 || strncmp(hid, "HID:", 4) != 0) {
		die(-1, "Unable to extract HID.");
	}
	if (sscanf(sids, "%s", sid) < 1 || strncmp(sid, "SID:", 4) != 0) {
		die(-1, "Unable to extract SID.");
	}
	return 1;	
}

int XgetServADHID(const char *name, char *ad, char *hid) 
{
	sockaddr_x dag;
	socklen_t daglen = sizeof(dag);
	char sdag[1024];
	if (XgetDAGbyName(name, &dag, &daglen) < 0)
		die(-1, "unable to locate: %s\n", name);
	Graph g(&dag);
	strncpy(sdag, g.dag_string().c_str(), sizeof(sdag));
	// say("sdag = %s\n",sdag);
	char *ads = strstr(sdag, "AD:");	// first occurrence
	char *hids = strstr(sdag, "HID:");

	if (sscanf(ads, "%s", ad) < 1 || strncmp(ad, "AD:", 3) != 0) {
		die(-1, "Unable to extract AD.");
	}
	if (sscanf(hids, "%s", hid) < 1 || strncmp(hid, "HID:", 4) != 0) {
		die(-1, "Unable to extract HID.");
	}

	return 1;
}

int initDatagramClient(const char *name, struct addrinfo *ai, sockaddr_x *sa) 
{
	if (Xgetaddrinfo(name, NULL, NULL, &ai) != 0)
		die(-1, "unable to lookup name %s\n", name);
	sa = (sockaddr_x*)ai->ai_addr;

	Graph g(sa);
	printf("\n%s\n", g.dag_string().c_str());

	int sock;
	if ((sock = Xsocket(AF_XIA, SOCK_DGRAM, 0)) < 0) {
		die(-2, "unable to create the socket\n");
	}
	say("Xsock %4d created\n", sock);

	return sock;
}

int initStreamClient(const char *name, char *src_ad, char *src_hid, char *dst_ad, char *dst_hid) 
{
	int sock, rc;
	sockaddr_x dag;
	socklen_t daglen;
	char sdag[1024];
	char IP[MAX_XID_SIZE];

	// lookup the xia service 
	daglen = sizeof(dag);
//cerr<<"Before got DAG by name: "<<name<<"\n";
	if (XgetDAGbyName(name, &dag, &daglen) < 0)
		die(-1, "unable to locate: %s\n", name);
//cerr<<"Got DAG by name\n";
	// create a socket, and listen for incoming connections
	if ((sock = Xsocket(AF_XIA, SOCK_STREAM, 0)) < 0)
		die(-1, "Unable to create the listening socket\n");
//cerr<<"Created socket\n";    
	if (Xconnect(sock, (struct sockaddr*)&dag, daglen) < 0) {
		Xclose(sock);
		die(-1, "Unable to bind to the dag: %s\n", dag);
	}
//cerr<<"Connected to peer\n";
	rc = XreadLocalHostAddr(sock, src_ad, MAX_XID_SIZE, src_hid, MAX_XID_SIZE, IP, MAX_XID_SIZE);

	if (rc < 0) {
		Xclose(sock);
		die(-1, "Unable to read local address.\n");
	} 
	else{
		warn("My AD: %s, My HID: %s\n", src_ad, src_hid);
	}
	
	// save the AD and HID for later. This seems hacky we need to find a better way to deal with this
	Graph g(&dag);
	strncpy(sdag, g.dag_string().c_str(), sizeof(sdag));
	// say("sdag = %s\n",sdag);
	char *ads = strstr(sdag, "AD:");
	char *hids = strstr(sdag, "HID:");
	
	if (sscanf(ads, "%s", dst_ad) < 1 || strncmp(dst_ad, "AD:", 3) != 0) {
		die(-1, "Unable to extract AD.");
	}
	if (sscanf(hids, "%s", dst_hid) < 1 || strncmp(dst_hid, "HID:", 4) != 0) {
		die(-1, "Unable to extract HID.");
	}
	warn("Service AD: %s, Service HID: %s\n", dst_ad, dst_hid);
	return sock;
}

int registerDatagramReceiver(char* name) 
{
	int sock;

	if ((sock = Xsocket(AF_XIA, SOCK_DGRAM, 0)) < 0)
		die(-2, "unable to create the datagram socket\n");

	char sid_string[strlen("SID:") + XIA_SHA_DIGEST_STR_LEN];
	// Generate an SID to use
	if (XmakeNewSID(sid_string, sizeof(sid_string))) {
		die(-1, "Unable to create a temporary SID");
	}

	struct addrinfo *ai;
	if (Xgetaddrinfo(NULL, sid_string, NULL, &ai) != 0)
		die(-1, "getaddrinfo failure!\n");

	sockaddr_x *sa = (sockaddr_x*)ai->ai_addr;

	Graph g((sockaddr_x*)ai->ai_addr);
	printf("\nDatagram DAG\n%s\n", g.dag_string().c_str());

	if (XregisterName(name, sa) < 0)
		die(-1, "error registering name: %s\n", name);

	say("registered name: \n%s\n", name);

	if (Xbind(sock, (sockaddr *)sa, sizeof(sa)) < 0) {
		die(-3, "unable to bind to the dag\n");
	}
	return sock;
}

int registerStreamReceiver(char* name, char *myAD, char *myHID, char *my4ID) 
{
	int sock;

	// create a socket, and listen for incoming connections
	if ((sock = Xsocket(AF_XIA, SOCK_STREAM, 0)) < 0)
		die(-1, "Unable to create the listening socket\n");

	// read the localhost AD and HID
	if (XreadLocalHostAddr(sock, myAD, sizeof(myAD), myHID, sizeof(myHID), my4ID, sizeof(my4ID)) < 0)
		die(-1, "Reading localhost address\n");

	char sid_string[strlen("SID:") + XIA_SHA_DIGEST_STR_LEN];
	// Generate an SID to use
	if (XmakeNewSID(sid_string, sizeof(sid_string))) {
		die(-1, "Unable to create a temporary SID");
	}

	struct addrinfo hints, *ai;
	bzero(&hints, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_XIA;

	if (Xgetaddrinfo(NULL, sid_string, &hints, &ai) != 0)
		die(-1, "getaddrinfo failure!\n");

	Graph g((sockaddr_x*)ai->ai_addr);

	sockaddr_x *sa = (sockaddr_x*)ai->ai_addr;

	if (Xbind(sock, (struct sockaddr*)sa, sizeof(sockaddr_x)) < 0) {
		Xclose(sock);
		 die(-1, "Unable to bind to the dag: %s\n", g.dag_string().c_str());
	}
	say("listening on dag: %s\n", g.dag_string().c_str());

	Xlisten(sock, 5);

	if (XregisterName(name, sa) < 0 ) {
		die(-1, "error registering name: %s\n", name);
	}
	say("\nRegistering DAG with nameserver:\n%s\n", g.dag_string().c_str());

  return sock;
}

void *blockListener(void *listenID, void *recvFuntion (void *)) 
{
  int listenSock = *((int*)listenID);
  int acceptSock;

  while (1) {
		say("Waiting for a client connection\n");
   		
		if ((acceptSock = Xaccept(listenSock, NULL, NULL)) < 0){
			die(-1, "accept failed\n");
		}
		say("connected\n");
		
		pthread_t client;
		pthread_create(&client, NULL, recvFuntion, (void *)&acceptSock);
	}
	
	Xclose(listenSock);

	return NULL;
}

int getIndex(string target, vector<string> pool) {
	for (unsigned i = 0; i < pool.size(); i++) {
		if (target == pool[i])
			return i;
	}
	return -1;
}

int registerPrefetchService(const char *name, char *src_ad, char *src_hid, char *dst_ad, char *dst_hid) 
{
	int sock, rc;
	sockaddr_x dag;
	socklen_t daglen;
	char sdag[1024];
	char IP[MAX_XID_SIZE];

	// lookup the xia service 
	daglen = sizeof(dag);
	if (XgetDAGbyName(name, &dag, &daglen) < 0)
		die(-1, "unable to locate: %s\n", name);
	// create a socket, and listen for incoming connections
	if ((sock = Xsocket(AF_XIA, SOCK_STREAM, 0)) < 0)
		die(-1, "Unable to create the listening socket\n");
	if (Xconnect(sock, (struct sockaddr*)&dag, daglen) < 0) {
		Xclose(sock);
		die(-1, "Unable to bind to the dag: %s\n", dag);
	}
	rc = XreadLocalHostAddr(sock, src_ad, MAX_XID_SIZE, src_hid, MAX_XID_SIZE, IP, MAX_XID_SIZE);

	if (rc < 0) {
		Xclose(sock);
		die(-1, "Unable to read local address.\n");
	} 
	else{
		warn("My AD: %s, My HID: %s\n", src_ad, src_hid);
	}
	
	// save the AD and HID for later. This seems hacky we need to find a better way to deal with this
	Graph g(&dag);
	strncpy(sdag, g.dag_string().c_str(), sizeof(sdag));
	// say("sdag = %s\n",sdag);
	char *ads = strstr(sdag, "AD:");
	char *hids = strstr(sdag, "HID:");
	
	if (sscanf(ads, "%s", dst_ad) < 1 || strncmp(dst_ad, "AD:", 3) != 0) {
		die(-1, "Unable to extract AD.");
	}
	if (sscanf(hids, "%s", dst_hid) < 1 || strncmp(dst_hid, "HID:", 4) != 0) {
		die(-1, "Unable to extract HID.");
	}
	warn("Service AD: %s, Service HID: %s\n", dst_ad, dst_hid);

	return sock;
}

int registerPrefetchManager(const char *name) 
{
	int sock;
	sockaddr_x dag;
	socklen_t daglen;

	// lookup the xia service 
	daglen = sizeof(dag);

	if (XgetDAGbyName(name, &dag, &daglen) < 0)
		die(-1, "unable to locate: %s\n", name);
	// create a socket, and listen for incoming connections
	if ((sock = Xsocket(AF_XIA, SOCK_STREAM, 0)) < 0)
		die(-1, "Unable to create the listening socket\n");
	if (Xconnect(sock, (struct sockaddr*)&dag, daglen) < 0) {
		Xclose(sock);
		die(-1, "Unable to bind to the dag: %s\n", dag);
	}
	return sock;
}

int updateManifest(int sock, vector<string> CIDs) 
{
	char cmd[XIA_MAX_BUF];
	memset(cmd, '\0', strlen(cmd));
	char cids[XIA_MAX_BUF];
	memset(cids, '\0', strlen(cids));

	for (unsigned int i = 0; i < CIDs.size(); i++) {
		strcat(cids, " ");
		strcat(cids, string2char(CIDs[i]));
	}	
	// TODO: check total length of cids should not exceed max buf
	sprintf(cmd, "reg%s", cids);
	int n = sendStreamCmd(sock, cmd);

	return n;
}

// TODO: XIA_MAX_BUF needs to be augmented
int XrequestChunkPrefetch(int sock, const ChunkStatus *cs) {
	char cmd[XIA_MAX_BUF];
	memset(cmd, '\0', strlen(cmd));

	// TODO: check total length of cids should not exceed max buf
	sprintf(cmd, "fetch %ld %s", cs[0].cidLen, cs[0].cid);
	int n = sendStreamCmd(sock, cmd); 	
	return n;
}

// TODO: right now it's hacky, need to fix the way reading XIDs when including fallback DAG
char *chunkReqDag2cid(char *dag) {
	size_t cidLen;
	char *AD = (char *)malloc(512); 		
	char *HID = (char *)malloc(512); 
	char *CID = (char *)malloc(512); 
	sscanf(dag, "%ld RE ( %s %s ) CID:%s", &cidLen, AD, HID, CID);
	free(AD);
	free(HID);
	return CID;
}