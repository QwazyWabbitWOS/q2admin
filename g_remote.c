#include "g_local.h"

remote_t remote;
cvar_t	*udpport;

void RA_Send(remote_cmd_t cmd, const char *fmt, ...) {

	va_list     argptr;
    char        string[MAX_STRING_CHARS];
	size_t      len;
	
	if (!(remote.enabled && remote.online)) {
		return;
	}
	
	va_start(argptr, fmt);
    len = vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);
	
	if (len >= sizeof(string)) {
        return;
    }
	
	RA_InitBuffer();
	RA_WriteByte(cmd);
	RA_WriteString(remoteKey);
	RA_WriteString(string);

	int r = sendto(
		remote.socket, 
		remote.msg,
		remote.msglen, 
		MSG_DONTWAIT, 
		remote.addr->ai_addr, 
		remote.addr->ai_addrlen
	);
	
	if (r == -1) {
		gi.dprintf("[RA] error sending data: %s\n", strerror(errno));
	}
}


void RA_Init() {
	
	memset(&remote, 0, sizeof(remote));
	maxclients = gi.cvar("maxclients", "64", CVAR_LATCH);
	
	if (!remoteEnabled) {
		gi.dprintf("Remote Admin is disabled in your config file.\n");
		return;
	}
	
	gi.dprintf("[RA] Remote Admin Init...\n");
	
	struct addrinfo hints, *res = 0;
	memset(&hints, 0, sizeof(hints));
	memset(&res, 0, sizeof(res));
	
	hints.ai_family         = AF_INET;   	// either v6 or v4
	hints.ai_socktype       = SOCK_DGRAM;	// UDP
	hints.ai_protocol       = 0;
	hints.ai_flags          = AI_ADDRCONFIG;
	
	gi.dprintf("[RA] looking up %s... ", remoteAddr);

	int err = getaddrinfo(remoteAddr, va("%d",remotePort), &hints, &res);
	if (err != 0) {
		gi.dprintf("error, disabling\n");
		remote.enabled = 0;
		return;
	} else {
		char address[INET_ADDRSTRLEN];
		inet_ntop(res->ai_family, &((struct sockaddr_in *)res->ai_addr)->sin_addr, address, sizeof(address));
		gi.dprintf("%s\n", address);
	}
	
	int fd = socket(res->ai_family, res->ai_socktype, IPPROTO_UDP);
	if (fd == -1) {
		gi.dprintf("Unable to open socket to %s:%d...disabling remote admin\n", remoteAddr, remotePort);
		remote.enabled = 0;
		return;
	}
	
	remote.socket = fd;
	remote.addr = res;
	remote.flags = remoteFlags;
	remote.enabled = 1;
	remote.online = 1;
}


void RA_RunFrame() {
	
	if (!remote.enabled) {
		return;
	}

	uint8_t i;

	// report server if necessary
	if (remote.next_report <= remote.frame_number) {
		//RA_Send(CMD_SHEARTBEAT, "%s\\%d\\%s\\%d\\%d", remote.mapname, remote.maxclients, remote.rcon_password, remote.port, remote.flags);
		remote.next_report = remote.frame_number + SECS_TO_FRAMES(60);
	}


	for (i=0; i<=remote.maxclients; i++) {
		if (proxyinfo[i].inuse) {

			if (proxyinfo[i].next_report <= remote.frame_number) {
				//RA_Send(CMD_PHEARTBEAT, "%d\\%s", i, proxyinfo[i].userinfo);
				proxyinfo[i].next_report = remote.frame_number + SECS_TO_FRAMES(60);
			}

			/*
			if (!proxyinfo[i].remote_reported) {
				RA_Send(CMD_CONNECT, "%d\\%s", i, proxyinfo[i].userinfo);
				proxyinfo[i].remote_reported = 1;
			}

			// replace player edict's die() pointer
			if (*proxyinfo[i].ent->die != PlayerDie_Internal) {
				proxyinfo[i].die = *proxyinfo[i].ent->die;
				proxyinfo[i].ent->die = &PlayerDie_Internal;
			}
			*/
		}
	}

	remote.frame_number++;
}

void RA_Shutdown() {
	if (!remote.enabled) {
		return;
	}

	gi.cprintf(NULL, PRINT_HIGH, "[RA] Unregistering with remote admin server\n\n");
	RA_Send(CMD_QUIT, "");
	freeaddrinfo(remote.addr);
}


void PlayerDie_Internal(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point) {
	uint8_t id = getEntOffset(self) - 1;
	uint8_t aid = getEntOffset(attacker) - 1;
	
	if (self->deadflag != DEAD_DEAD) {	
		gi.dprintf("self: %s\t inflictor: %s\t attacker %s\n", self->classname, inflictor->classname, attacker->classname);
		
		// crater, drown (water, acid, lava)
		if (g_strcmp0(attacker->classname, "worldspawn") == 0) {
			//RA_Send(CMD_FRAG,"%d\\%d\\worldspawn", id, aid);
		} else if (g_strcmp0(attacker->classname, "player") == 0 && attacker->client) {
			//gi.dprintf("Attacker: %s\n", attacker->client->pers.netname);
			/*RA_Send(CMD_FRAG, "%d\\%d\\%s", id, aid,
				attacker->client->pers.weapon->classname
			);*/
		}
	}
	
	proxyinfo[id].die(self, inflictor, attacker, damage, point);
}

uint16_t getport(void) {
	static cvar_t *port;

	port = gi.cvar("port", "0", 0);
	if ((int) port->value) {
		return (int) port->value;
	}
	
	port = gi.cvar("net_port", "0", 0);
	if ((int) port->value) {
		return (int) port->value;
	}
	
	port = gi.cvar("port", "27910", 0);
	return (int) port->value;
}

void RA_InitBuffer() {
	q2a_memset(&remote.msg, 0, sizeof(remote.msg));
	remote.msglen = 0;
}

void RA_WriteByte(uint8_t b) {
	remote.msg[remote.msglen] = b & 0xff;
	remote.msglen++;
}

void RA_WriteString(char *str) {
	
	uint16_t i;
	size_t len;
	len = strlen(str);
	
	if (!str || len == 0) {
		RA_WriteByte(0);
		return;
	}
	
	if (len > MAX_MSG_LEN - remote.msglen) {
		RA_WriteByte(0);
		return;
	}
	
	while (*str) {
		remote.msg[remote.msglen++] = *str++ | 0x80;
	}

	RA_WriteByte(0);
}

void RA_Register(void) {
	RA_Send(CMD_REGISTER, "serverstarup");
}

void RA_Unregister(void) {
	RA_Send(CMD_QUIT, "serverquit");
}

void RA_PlayerConnect(edict_t *ent) {
	
}

void RA_PlayerDisconnect(edict_t *ent) {
	
}