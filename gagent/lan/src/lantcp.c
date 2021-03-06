#include "gagent.h"
#include "lan.h"

/****************************************************************
        FunctionName        :   Lan_setClientTimeOut.
        Description         :      setting new tcp client timeout.
        Add by Will.zhou     --2015-03-10
****************************************************************/
void Lan_setClientTimeOut(pgcontext pgc, int32 channel)
{
    pgc->ls.tcpClient[channel].timeout = LAN_CLIENT_MAXLIVETIME;
}

int32 Lan_AddTcpNewClient(pgcontext pgc, int fd, struct sockaddr_t *addr)
{
    int32 i;
    
    if(fd < 0)
    {
        return RET_FAILED;
    }

    for(i = 0; i < LAN_TCPCLIENT_MAX; i++)
    {
        if(pgc->ls.tcpClient[i].fd == -1)
        {
            pgc->ls.tcpClient[i].fd = fd;
            Lan_setClientTimeOut(pgc, i);
			
            return RET_SUCCESS;
        }
    }

    GAgent_Printf(GAGENT_DEBUG, "[LAN]tcp client over %d channel, denied!", LAN_TCPCLIENT_MAX);
    close(fd);
    
    return RET_FAILED;
}

/****************************************************************
        FunctionName        :   Lan_TcpServerHandler.
        Description         :      Lan handing new tcp client.
        Add by Will.zhou     --2015-03-10
****************************************************************/
int32 Lan_TcpServerHandler(pgcontext pgc)
{
    int newfd, ret = RET_FAILED;
    struct sockaddr_t addr;
    int addrLen = sizeof(struct sockaddr_t);
    
    if(pgc->ls.tcpServerFd < 0)
    {
        return RET_FAILED;
    }
    if(FD_ISSET(pgc->ls.tcpServerFd, &(pgc->rtinfo.readfd)))
    {
        /* if nonblock, can be done in accept progress */
        newfd = Socket_accept(pgc->ls.tcpServerFd, &addr, &addrLen);
        if(newfd > 0)
        {
            GAgent_Printf(GAGENT_DEBUG, "detected new client as %d", newfd);
            ret = Lan_AddTcpNewClient(pgc, newfd, &addr);
        }

    }
    return ret;
}

int32 LAN_readPacket(int32 fd, ppacket pbuf, int32 bufLen)
{
    int dataLenth = 0;
    
    resetPacket( pbuf );
    memset(pbuf->phead, 0, bufLen);
    
    dataLenth = recv(fd, pbuf->phead, bufLen, 0);

    return dataLenth;
    
}

int32 Lan_tcpClientDataHandle(pgcontext pgc, uint32 channel, 
                ppacket prxBuf, ppacket ptxBuf, int32 buflen)
{
    int32 fd = pgc->ls.tcpClient[channel].fd;
    int32 recDataLen =0;
    
    recDataLen = LAN_readPacket(fd, prxBuf, buflen);

    if(recDataLen <= 0)
    {
        return RET_FAILED;
    }

    Lan_setClientTimeOut(pgc, channel);
    return Lan_dispatchTCPData(pgc, prxBuf, ptxBuf, channel);
}

/****************************************************************
        FunctionName        :   Lan_handleLogin.
        Description         :      Lan Tcp logining.
        Add by Will.zhou     --2015-03-10
****************************************************************/
void Lan_handleLogin( pgcontext pgc, ppacket src, ppacket dest, int clientIndex)
{
    int i;
    int ret;
    int32 fd;
    uint8 isLogin;
    uint8 *pbuf;

    resetPacket( dest );
    pbuf = dest->phead;
    fd = pgc->ls.tcpClient[clientIndex].fd;

    /* verify passcode */
    if( !memcmp((src->phead + 10), pgc->gc.wifipasscode, 10) )
    {
        /* login success */
        isLogin = LAN_CLIENT_LOGIN_SUCCESS;
        pgc->ls.tcpClientNums++;
        GAgent_Printf(GAGENT_INFO, "LAN login success! clientid[%d] ",clientIndex);
    }
    else
    {
        isLogin = LAN_CLIENT_LOGIN_FAIL;
        GAgent_Printf(GAGENT_WARNING,"LAN login fail. your passcode:%s",
                        src->phead + 10);
       
        GAgent_Printf(GAGENT_INFO, "expected passcode:%s", pgc->gc.wifipasscode);
    }
    /* protocol version */
     pbuf[0] = 0x00;
     pbuf[1] = 0x00;
     pbuf[2] = 0x00;
     pbuf[3] = 0x03;

     /* len */
     pbuf[4] = 0x04;

     /* flag */
     pbuf[5] = 0x00;

     /* cmd */
     pbuf[6] = 0x00;
     pbuf[7] = 0x09;

     /* login result */
     pbuf[8] = isLogin;

     pgc->ls.tcpClient[clientIndex].isLogin = isLogin;

     send(pgc->ls.tcpClient[clientIndex].fd, pbuf, 9, 0);
    
}

/****************************************************************
        FunctionName        :   Lan_handlePasscode.
        Description         :      reponsing passcode to client for Binding.
        Add by Will.zhou     --2015-03-10
****************************************************************/
void Lan_handlePasscode( pgcontext pgc, ppacket src, int clientIndex)
{
    int i;
    int ret;
    int32 fd;
    uint8 *pbuf;

    resetPacket(src);
    pbuf = src->phead;
    fd = pgc->ls.tcpClient[clientIndex].fd;

    /* protocol version */
    pbuf[0] = 0x00;
    pbuf[1] = 0x00;
    pbuf[2] = 0x00;
    pbuf[3] = 0x03;

    /* len */
    pbuf[4] = 0x0f;

    /* flag */
    pbuf[5] = 0x00;

    /* cmd */
    pbuf[6] = 0x00;
    pbuf[7] = 0x07;

    /* passcode len */
    pbuf[8] = 0x00;
    pbuf[9] = 0x0a;

    /* passcode */
    for(i=0;i<pbuf[9];i++)
    {
    	pbuf[10+i] = pgc->gc.wifipasscode[i];
    }

    ret = send(fd, pbuf, 20, 0);
    GAgent_Printf(GAGENT_INFO,"Send passcode(%s) to client[%d][send data len:%d] ", 
        pgc->gc.wifipasscode, fd, ret);

    return;
}

/****************************************************************
        FunctionName        :   Lan_AckHeartbeak.
        Description         :      Gagent response client heartbeat
        Add by Will.zhou     --2015-03-10
****************************************************************/
void Lan_AckHeartbeak( pgcontext pgc, ppacket src, int clientIndex )
{
    int32 fd;
    uint8 *pbuf;

    resetPacket(src);
    pbuf = src->phead;
    fd = pgc->ls.tcpClient[clientIndex].fd;

    /* protocol version */
    pbuf[0] = 0x00;
    pbuf[1] = 0x00;
    pbuf[2] = 0x00;
    pbuf[3] = 0x03;

    /* len */
    pbuf[4] = 0x03;

    /* flag */
    pbuf[5] = 0x00;

    /* cmd */
    pbuf[6] = 0x00;
    pbuf[7] = 0x16;

    send( fd, pbuf, 8, 0);
}

/****************************************************************
        FunctionName        :   Lan_dispatchTCPData.
        Description         :      parse and dispatch tcp cmd message.
        Add by Will.zhou     --2015-03-10
****************************************************************/
int32 Lan_dispatchTCPData(pgcontext pgc, ppacket prxBuf, ppacket ptxBuf, int32 clientIndex)
{
    int datalen;
    int ret = 0;
    uint16 cmd;
    int32 bytesOfLen;
    int len;

    bytesOfLen = mqtt_num_rem_len_bytes(prxBuf->phead + 3);
    len = mqtt_parse_rem_len(prxBuf->phead + 3);

    cmd = *(uint16 *)(prxBuf->phead + LAN_PROTOCOL_HEAD_LEN + LAN_PROTOCOL_FLAG_LEN
                        + bytesOfLen);
    cmd = ntohs(cmd);

    prxBuf->type = SetPacketType( prxBuf->type,LAN_TCP_DATA_IN,1 );
    ret = ParsePacket(prxBuf);

    ret = 0;
    switch (cmd)
    {
    case GAGENT_LAN_CMD_BINDING :
        Lan_handlePasscode(pgc, ptxBuf, clientIndex);
        break;
    case GAGENT_LAN_CMD_LOGIN:
        Lan_handleLogin(pgc, prxBuf, ptxBuf, clientIndex);
        break;
    case GAGENT_LAN_CMD_TRANSMIT:
        prxBuf->type = SetPacketType( prxBuf->type, LAN_TCP_DATA_IN, 0 );
        prxBuf->type = SetPacketType( ptxBuf->type, LOCAL_DATA_OUT, 1 );
        if((prxBuf->pend - prxBuf->ppayload) > 0)
           ret = prxBuf->pend - prxBuf->ppayload;
        else
           ret = 0;
        break;
    case GAGENT_LAN_CMD_HOSTPOTS:
        /* 
        Lan_GetWifiHotspots();
        */
        break;
    case GAGENT_LAN_CMD_LOG:
        break;
    case GAGENT_LAN_CMD_INFO:
        break;
    case GAGENT_LAN_CMD_TICK:
        Lan_AckHeartbeak(pgc, ptxBuf, clientIndex);
        break;
    case GAGENT_LAN_CMD_TEST:
        /*
        Lan_AckExitTestMode();
        */
        break;
    default:
        break;
    }
    return ret;
}

/****************************************************************
        FunctionName        :   LAN_tcpClientInit.
        Description         :      init tcp clients.
        Add by Will.zhou     --2015-03-10
****************************************************************/
int32 LAN_tcpClientInit(pgcontext pgc)
{
    int32 i;

    for (i = 0; i < LAN_TCPCLIENT_MAX; i++)
    {
        memset(&(pgc->ls.tcpClient[i]), 0x0, sizeof(pgc->ls.tcpClient[i]));
        pgc->ls.tcpClient[i].fd = -1;
        pgc->ls.tcpClient[i].timeout = 0;
    }

    return  RET_SUCCESS;
}

/****************************************************************
        FunctionName        :   Lan_CreateTCPServer.
        Description         :      create tcp server.
        Add by Will.zhou     --2015-03-10
****************************************************************/
void Lan_CreateTCPServer(int32 *pFd, int32 tcp_port)
{
    int bufferSize;
    struct sockaddr_t addr;

    if (*pFd == -1)
    {
        *pFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
       
        if(*pFd < 0)
        {
            GAgent_Printf(GAGENT_ERROR, "Create TCPServer failed!");
            *pFd = -1;
            return;
        }
        if(Gagent_setsocketnonblock(*pFd) != 0)
        {
            GAgent_Printf(GAGENT_ERROR,"TCP Server Gagent_setsocketnonblock fail.");
        }

        bufferSize = SOCKET_TCPSOCKET_BUFFERSIZE;
        setsockopt(*pFd, SOL_SOCKET, SO_RCVBUF, &bufferSize, 4);
        setsockopt(*pFd, SOL_SOCKET,SO_SNDBUF, &bufferSize, 4);
        

        memset(&addr, 0x0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port=htons(tcp_port);
        addr.sin_addr.s_addr=INADDR_ANY;
        if(bind(*pFd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        {
            GAgent_Printf(GAGENT_ERROR, "TCPSrever socket bind error");
            close(*pFd);
            *pFd = -1;
            return;
        }

        if(listen(*pFd, LAN_TCPCLIENT_MAX) != 0)
        {
            GAgent_Printf(GAGENT_ERROR, "TCPServer socket listen error,errno:%d", errno);
            close(*pFd);
            *pFd = -1;
            return;
        }

    }

    GAgent_Printf(GAGENT_DEBUG,"TCP Server socketid:%d on port:%d", *pFd, tcp_port);
    return;
}

