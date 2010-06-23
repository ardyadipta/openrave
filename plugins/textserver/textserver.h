// Copyright (C) 2006-2008 Carnegie Mellon University (rdiankov@cs.cmu.edu)
//
// This file is part of OpenRAVE.
// OpenRAVE is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#ifndef RAVE_SERVER
#define RAVE_SERVER

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <sstream>

#ifdef _WIN32
#define CLOSESOCKET closesocket
typedef int socklen_t;
#else
#include <fcntl.h>
#define CLOSESOCKET close
#endif

/// manages all connections. At the moment, server can handle only one connectino at a time
class SimpleTextServer : public ProblemInstance
{
    // socket just accepts connections
    class Socket
    {
    public: 
        struct PACKET
        {
            string cmd, arg;
        };

        Socket() {
            bInit = false;
            client_sockfd = 0;
        }
        ~Socket() {
            if( bInit )
                Close();
        }
        bool Accept(int server_sockfd)
        {
            if( bInit )
                Close();
    
            bool success = true;

            //signal(SIGCHLD, SIG_IGN); 
            //RAVELOG(L"server waiting for connection, %d\n", server_sockfd);

            //	char str[sizeof(server_address)+1];
            //	memcpy(str, &server_address, sizeof(server_address));
            //	str[sizeof(server_address)] = 0;

            client_len = sizeof(client_address);
            client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_address, (socklen_t*)&client_len);

            if( client_sockfd == -1 ) {
                success = false;
                client_sockfd = 0;
            }

            bInit = success;

            return bInit;
        }
        void Close()
        {
            if( bInit ) {
                // close
                CLOSESOCKET(client_sockfd); client_sockfd = 0;
                bInit = false;
            }
        }

        /// returns true if connection is valid, otherwise false
        bool IsInit() { return bInit; }

        void SendData(const void* pdata, int size_to_write)
        {
            if( client_sockfd == 0 )
                return;

            int nBytesReceived;

#ifndef _WIN32
            // check if closed, only for linux systems
            struct timeval tv;
            fd_set exfds, writefds;

            tv.tv_sec = 0;
            tv.tv_usec = 0;
	
            FD_ZERO(&exfds);
            FD_ZERO(&writefds);
            FD_SET(client_sockfd, &exfds);
    
            // don't care about writefds and exceptfds:
            int num = select(client_sockfd+1, NULL, NULL, &exfds, &tv);
    
            if ( num > 0 && FD_ISSET(client_sockfd, &exfds) ) {
                RAVELOG_ERRORA("socket exception detected\n");
                Close();
                return;
            }

            FD_SET(client_sockfd, &writefds);
    
            // don't care about writefds and exceptfds:
            num = select(client_sockfd+1, NULL, &writefds, NULL, &tv);
    
            if ( num == 0 || !FD_ISSET(client_sockfd, &writefds) ) {
                RAVELOG_WARNA("no writable socket\n");
                return;
            }

            /*int ret = recv(client_sockfd, NULL, 0, MSG_PEEK|MSG_DONTWAIT);
              if( ret == 0 ) {
              RAVEPRINT(L"socket close detected\n");
              Close();
              return;
              }*/
#endif
        
            char* pbuf = (char*)pdata;

            if( (nBytesReceived = send(client_sockfd, (char*)&size_to_write, 4, 0)) != 4 ) {
                RAVELOG_ERRORA("failed to send command: %d\n", nBytesReceived);
                return;
            }

            while(size_to_write > 0 ) {
                nBytesReceived = send(client_sockfd, pbuf, size_to_write, 0);
                if( nBytesReceived <= 0 ) {
                    if( nBytesReceived == -1 )
                        return;

                    //perror("failed to read line");
                    continue;
                }

                size_to_write -= nBytesReceived;
                pbuf += nBytesReceived;
            }
        }


        bool ReadLine(string& s)
        {
            struct timeval tv;
            fd_set readfds, exfds;
            s.resize(0);

            tv.tv_sec = 0;
            tv.tv_usec = 0;

    	
            FD_ZERO(&exfds);
            FD_SET(client_sockfd, &exfds);
    
            // don't care about writefds and exceptfds:
            int num = select(client_sockfd+1, NULL, NULL, &exfds, &tv);
    
            if ( num > 0 && FD_ISSET(client_sockfd, &exfds) ) {
                RAVELOG_ERRORA("socket exception detected\n");
                Close();
                return false;
            }

            FD_ZERO(&readfds);
            FD_SET(client_sockfd, &readfds);
    
            // don't care about writefds and exceptfds:
            num = select(client_sockfd+1, &readfds, NULL, NULL, &tv);
    
            if ( num == 0 || !FD_ISSET(client_sockfd, &readfds) ) {
                return false;
            }
	
            // protocol: size1 size2 "size1 bytes" "size2 bytes"
            long nBytesReceived; 
            char c;
            int failed = 0;

            while(1) {
                if( (nBytesReceived = recv(client_sockfd, &c, sizeof(char), 0)) > 0 ) {
                    if( c == '\n' || c == '\r')
                        break;
                    s.push_back(c);
                }
                else if( nBytesReceived == 0 ) {
                    //RAVELOG_DEBUGA("closing connection\n");
                    //  Close();
                    return false;
                }
                else {
                    if( failed < 10 ) {    
                        failed++;
                        Sleep(1);
                        continue;
                    }
                    perror("failed to read line");
                    Close();
                    return false;
                }
            }

            return true;
        }

    private:
        int client_sockfd;	
        int client_len;

        struct sockaddr_in client_address;
        bool bInit;
    };
    typedef boost::shared_ptr<Socket> SocketPtr;
    typedef boost::shared_ptr<Socket const> SocketConstPtr;

    /// \param in is the data passed from the network
    /// \param out is the return data that will be passed to the client
    /// \param boost::shared_ptr<void> is a pointer to a void that willl be passed to the worker thread function
    typedef boost::function<bool(istream&, ostream&, boost::shared_ptr<void>&)> OpenRaveNetworkFn;
    typedef boost::function<bool(boost::shared_ptr<istream>, boost::shared_ptr<void>)> OpenRaveWorkerFn;

    /// each network function has a function to intially processes the data on the socket function
    /// and one that is executed on the main worker thread to avoid multithreading data synchronization issues
    struct RAVENETWORKFN
    {
    RAVENETWORKFN() : bReturnResult(false) {}
    RAVENETWORKFN(const OpenRaveNetworkFn& socket, const OpenRaveWorkerFn& worker, bool bReturnResult) : fnSocketThread(socket), fnWorker(worker), bReturnResult(bReturnResult) {}

        OpenRaveNetworkFn fnSocketThread;
        OpenRaveWorkerFn fnWorker;
        bool bReturnResult; // if true, function is expected to return a result
    };

 public:
 SimpleTextServer(EnvironmentBasePtr penv) : ProblemInstance(penv) {
        _nIdIndex = 1;
        _nNextFigureId = 1;
        _bWorking = false;
        bDestroying = false;
        
        mapNetworkFns["body_checkcollision"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvCheckCollision, this, _1, _2, _3), OpenRaveWorkerFn(), true);
        mapNetworkFns["body_getjoints"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orBodyGetJointValues, this,_1, _2, _3), OpenRaveWorkerFn(), true);
        mapNetworkFns["body_destroy"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orBodyDestroy,this,_1,_2,_3), OpenRaveWorkerFn(), false);
        mapNetworkFns["body_enable"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orBodyEnable,this,_1,_2,_3), OpenRaveWorkerFn(), false);
        mapNetworkFns["body_getaabb"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orBodyGetAABB,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["body_getaabbs"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orBodyGetAABBs,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["body_getlinks"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orBodyGetLinks,this,_1,_2,_3),OpenRaveWorkerFn(), true);
        mapNetworkFns["body_getdof"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orBodyGetDOF,this,_1,_2,_3),OpenRaveWorkerFn(), true);
        mapNetworkFns["body_settransform"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orKinBodySetTransform,this,_1,_2,_3),OpenRaveWorkerFn(), false);
        mapNetworkFns["body_setjoints"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orBodySetJointValues,this,_1,_2,_3), OpenRaveWorkerFn(), false);
        mapNetworkFns["body_setjointtorques"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orBodySetJointTorques,this,_1,_2,_3), OpenRaveWorkerFn(), false);
        mapNetworkFns["close"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvClose,this,_1,_2,_3), OpenRaveWorkerFn(),false);
        mapNetworkFns["createrobot"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvCreateRobot,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["createbody"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvCreateKinBody,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["createproblem"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvCreateProblem,this,_1,_2,_3), boost::bind(&SimpleTextServer::worEnvCreateProblem,this,_1,_2), true);
        mapNetworkFns["env_dstrprob"] = RAVENETWORKFN(OpenRaveNetworkFn(), boost::bind(&SimpleTextServer::worEnvDestroyProblem,this,_1,_2), false);
        mapNetworkFns["env_getbodies"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvGetBodies,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["env_getrobots"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvGetRobots,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["env_getbody"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvGetBody,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["env_loadplugin"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvLoadPlugin,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["env_raycollision"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvRayCollision,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["env_stepsimulation"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvStepSimulation,this,_1,_2,_3), boost::bind(&SimpleTextServer::worEnvStepSimulation,this,_1,_2), false);
        mapNetworkFns["env_triangulate"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvTriangulate,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["loadscene"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvLoadScene,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["plot"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvPlot,this,_1,_2,_3), OpenRaveWorkerFn(), true); 
        mapNetworkFns["problem_sendcmd"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orProblemSendCommand,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_checkselfcollision"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotCheckSelfCollision,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_controllersend"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotControllerSend,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_controllerset"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotControllerSet,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_getactivedof"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotGetActiveDOF,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_getdofvalues"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotGetDOFValues,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_getlimits"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotGetDOFLimits,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_getmanipulators"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotGetManipulators,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_getsensors"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotGetAttachedSensors,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_sensorsend"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotSensorSend,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_sensordata"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotSensorData,this,_1,_2,_3), OpenRaveWorkerFn(), true);
        mapNetworkFns["robot_setactivedofs"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotSetActiveDOFs,this,_1,_2,_3), OpenRaveWorkerFn(), false);
        mapNetworkFns["robot_setdof"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orRobotSetDOFValues,this,_1,_2,_3), OpenRaveWorkerFn(), false);
        mapNetworkFns["robot_traj"] = RAVENETWORKFN(OpenRaveNetworkFn(), boost::bind(&SimpleTextServer::worRobotStartActiveTrajectory,this,_1,_2), false);
        mapNetworkFns["render"] = RAVENETWORKFN(OpenRaveNetworkFn(), boost::bind(&SimpleTextServer::worRender,this,_1,_2), false);
        mapNetworkFns["setoptions"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvSetOptions,this,_1,_2,_3), boost::bind(&SimpleTextServer::worSetOptions,this,_1,_2), false);
        mapNetworkFns["test"] = RAVENETWORKFN(OpenRaveNetworkFn(), OpenRaveWorkerFn(), false);
        mapNetworkFns["wait"] = RAVENETWORKFN(boost::bind(&SimpleTextServer::orEnvWait,this,_1,_2,_3), OpenRaveWorkerFn(), true);

        string logfilename = GetEnv()->GetHomeDirectory() + string("/textserver.log");
        flog.open(logfilename.c_str());
        if( !!flog )
            RAVELOG_DEBUGA("logging network to %s.txt\n",logfilename.c_str());
    }

    virtual ~SimpleTextServer() {
        Destroy();
    }

    virtual int main(const std::string& cmd)
    {
        _nPort = 4765;
        stringstream ss(cmd);
        ss >> _nPort;

        Destroy();
  
#ifdef _WIN32
        WORD      wVersionRequested;
        WSADATA   wsaData;

        wVersionRequested = MAKEWORD(1,1);
        if (WSAStartup(wVersionRequested, &wsaData) != 0) {
            RAVELOG_ERROR("Failed to start win socket\n");
            return -1;
        }
#endif

        memset(&server_address, 0, sizeof(server_address));
        server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = htonl(INADDR_ANY);
        server_address.sin_port = htons(_nPort);
        server_len = sizeof(server_address);

        // this allows to immediately reconnect to OpenRave
        // when the program crashed and is rerun immediately
        int yes = 1;
        int err = setsockopt(server_sockfd, SOL_SOCKET,SO_REUSEADDR, (const char*)&yes, sizeof(int));
        if( err ) {
            RAVELOG_ERRORA("failed to set socket option, err=%d\n", err);
            perror("failed to set socket options\n");
            return -1;
        }

        err = bind(server_sockfd, (struct sockaddr *)&server_address, server_len);
        if( err ) {
            RAVELOG_ERRORA("failed to bind server to port %d, error=%d\n", _nPort, err);
            return -1;
        }

        err = listen(server_sockfd, 16);
        if( err ) {
            RAVELOG_ERRORA("failed to listen to server port %d, error=%d\n", _nPort, err);
            return -1;
        }

        // set nonblocking
#ifdef _WIN32
        u_long flags = 1;
        ioctlsocket(server_sockfd, FIONBIO, &flags);
#else
        int flags;

        // If they have O_NONBLOCK, use the Posix way to do it
#if defined(O_NONBLOCK)
        // Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5.
        if (-1 == (flags = fcntl(server_sockfd, F_GETFL, 0)))
            flags = 0;
        if( fcntl(server_sockfd, F_SETFL, flags | O_NONBLOCK) < 0 )
            return -1;
#else
        // Otherwise, use the old way of doing it
        flags = 1;
        if( ioctl(server_sockfd, FIOBIO, &flags) < 0 )
            return -1;
#endif
#endif

        RAVELOG_INFO("text server listening on port %d\n",_nPort);
        _servthread.reset(new boost::thread(boost::bind(&SimpleTextServer::_listen_threadcb,this)));
        _workerthread.reset(new boost::thread(boost::bind(&SimpleTextServer::_worker_threadcb,this)));
        bInitThread = true;
        return 0;
    }

    virtual void Destroy()
    {
        Reset();

        {
            boost::mutex::scoped_lock lock(_mutexWorker); // need lock to keep multiple threads out of Destroy
            if( bDestroying )
                return;
            bDestroying = true;
            _mapFigureIds.clear();
            _mapProblems.clear();
        }

        if( bInitThread ) {
            bCloseThread = true;
            _condWorker.notify_all();
            if( !!_servthread )
                _servthread->join();
            _servthread.reset();

            FOREACH(it, _listReadThreads) {
                _condWorker.notify_all();
                (*it)->join();
            }
            _listReadThreads.clear();
            _condHasWork.notify_all();
            if( !!_workerthread )
                _workerthread->join();
            _workerthread.reset();

            bCloseThread = false;
            bInitThread = false;
    
            CLOSESOCKET(server_sockfd); server_sockfd = 0;
        }
    
        bDestroying = false;
    }

    virtual void Reset()
    {
        {
            boost::mutex::scoped_lock lock(_mutexWorker);
            listWorkers.clear();
            _mapFigureIds.clear();
        }

        // wait for worker thread to stop
        while(_bWorking) {
            _condWorker.notify_all();
            Sleep(1);
        }
    }

    virtual bool SimulationStep(dReal fElapsedTime)
    {
        return false;
    }

 private:

    inline boost::shared_ptr<SimpleTextServer> shared_server() { return boost::static_pointer_cast<SimpleTextServer>(shared_from_this()); }
    inline boost::shared_ptr<SimpleTextServer const> shared_server_const() const { return boost::static_pointer_cast<SimpleTextServer const>(shared_from_this()); }

    // called from threads other than the main worker to wait until 
    void SyncWithWorkerThread()
    {
        boost::mutex::scoped_lock lock(_mutexWorker);
        while((listWorkers.size() > 0 || _bWorking) && !bCloseThread) {
            _condHasWork.notify_all();
            _condWorker.wait(lock);
        }
    }

    void ScheduleWorker(const boost::function<void()>& fn)
    {
        boost::mutex::scoped_lock lock(_mutexWorker);
        listWorkers.push_back(fn);
        _condHasWork.notify_all();
    }

    void _worker_threadcb()
    {
        list<boost::function<void()> > listlocalworkers;
        while(!bCloseThread) {
            {
                boost::mutex::scoped_lock lock(_mutexWorker);
                _condHasWork.wait(lock);
                if( bCloseThread )
                    break;

                if( listWorkers.size() == 0 ) {
                    _condWorker.notify_all();
                    continue;
                }
            
                *(volatile bool*)&_bWorking = true;
                listlocalworkers.swap(listWorkers);
            }

            // transfer the current workers to a temporary list so 
            FOREACH(it, listlocalworkers) {
                try {
                    (*it)();
                }
                catch(const openrave_exception& ex) {
                    RAVELOG_FATALA("server caught exception: %s\n",ex.what());
                }
                catch(const std::logic_error& ex) {
                    RAVELOG_FATALA("server caught std exception: %s\n",ex.what());
                }
                catch(...) {
                    RAVELOG_FATAL("unknown exception!!\n");
                }
            }
            listlocalworkers.clear();
    
            *(volatile bool*)&_bWorking = false;
            _condWorker.notify_all();
        }
    }

    void _listen_threadcb()
    {
        SocketPtr psocket(new Socket());

        while(!bCloseThread) {

            // finally initialize the socket
            if( !psocket->Accept(server_sockfd) ) {
                Sleep(100);
                continue;
            }

            // start a new thread
            _listReadThreads.push_back(boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&SimpleTextServer::_read_threadcb,shared_server(), psocket))));
            psocket.reset(new Socket());
        }

        RAVELOG_DEBUGA("**Server thread exiting\n");
    }

    void _read_threadcb(SocketPtr psocket)
    {
        RAVELOG_VERBOSEA("started new server connection\n");
        string cmd, line;
        stringstream sout;
        while(!bCloseThread) {
            //Sleep(100);
            if( psocket->ReadLine(line) && line.length() ) {
            
                if( !!flog && GetEnv()->GetDebugLevel()>0) {
                    static int index=0;
                    flog << index++ << ": " << line << endl;
                }

                boost::shared_ptr<istream> is(new stringstream(line));
                *is >> cmd;
                if( !*is ) {
                    RAVELOG_ERRORA("Failed to get command\n");
                    psocket->SendData("error\n",1);
                    continue;
                }
                std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
                stringstream::streampos inputpos = is->tellg();

                map<string, RAVENETWORKFN>::iterator itfn = mapNetworkFns.find(cmd);
                if( itfn != mapNetworkFns.end() ) {
                    bool bCallWorker = true;
                    boost::shared_ptr<void> pdata;

                    // need to set w.args before pcmdend is modified
                    sout.str(""); sout.clear();
                    if( !!itfn->second.fnSocketThread ) {
                        bool bSuccess = false;
                        try {
                            bSuccess = itfn->second.fnSocketThread(*is, sout, pdata);
                        }
                        catch(const openrave_exception& ex) {
                            RAVELOG_FATALA("server caught exception: %s\n",ex.what());
                        }
                        catch(const std::logic_error& ex) {
                            RAVELOG_FATALA("server caught std exception: %s\n",ex.what());
                        }
                        catch(...) {
                            RAVELOG_FATAL("unknown exception!!\n");
                        }

                        if( bSuccess ) {
                            if( itfn->second.bReturnResult )
                                psocket->SendData(sout.str().c_str(), sout.str().size());
                
                            if( !itfn->second.fnWorker )
                                bCallWorker = false;
                        }
                        else {
                            bCallWorker = false;
                            if( !!flog  ) {
                                flog << " error" << endl;
                            }
                            if( itfn->second.bReturnResult )
                                psocket->SendData("error\n", 6);
                        }
                    }
                    else {
                        if( itfn->second.bReturnResult )
                            psocket->SendData(sout.str().c_str(), sout.str().size()); // return dummy
                        bCallWorker = !!itfn->second.fnWorker;
                    }
                
                    if( bCallWorker ) {
                        BOOST_ASSERT(!!itfn->second.fnWorker);
                        is->clear();
                        is->seekg(inputpos);
                        ScheduleWorker(boost::bind(itfn->second.fnWorker,is,pdata));
                    }
                }
                else {
                    RAVELOG_ERRORA("Failed to recognize command: %s\n", cmd.c_str());
                    psocket->SendData("error\n",1);
                }
            }
            else if( !psocket->IsInit() )
                break;

            Sleep(1);
        }

        RAVELOG_VERBOSEA("Closing socket connection\n");
    }

    int _nPort; ///< port used for listening to incoming connections

    boost::shared_ptr<boost::thread> _servthread, _workerthread;
    list<boost::shared_ptr<boost::thread> > _listReadThreads;

    boost::mutex _mutexWorker;
    boost::condition _condWorker;
    boost::condition _condHasWork;

    bool bInitThread;
    bool bCloseThread;
    bool bDestroying;

    struct sockaddr_in server_address;
	int server_sockfd, server_len;

    ofstream flog;

    list<boost::function<void()> > listWorkers;
    map<string, RAVENETWORKFN> mapNetworkFns;

    int _nIdIndex;
    map<int, ProblemInstancePtr > _mapProblems;
    map<int, EnvironmentBase::GraphHandlePtr> _mapFigureIds;
    int _nNextFigureId;

    bool _bWorking; ///< worker thread processing current work items

protected:
    // all the server functions
    KinBodyPtr orMacroGetBody(istream& is)
    {
        int index=0;
        is >> index;
        if( !is )
            return KinBodyPtr();
        return GetEnv()->GetBodyFromEnvironmentId(index);
    }

    RobotBasePtr orMacroGetRobot(istream& is)
    {
        int index=0;
        is >> index;
        if( !is )
            return RobotBasePtr();

        KinBodyPtr pbody = GetEnv()->GetBodyFromEnvironmentId(index);
        if( !pbody || !pbody->IsRobot() )
            return RobotBasePtr();
        return boost::static_pointer_cast<RobotBase>(pbody);
    }

    /// orRender - Render the new OpenRAVE scene
    bool worRender(boost::shared_ptr<istream> is, boost::shared_ptr<void> pdata)
    {
        string cmd;
        while(1) {
            *is >> cmd;
            if( !*is )
                break;
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            if( cmd == "start" ) {
                GetEnv()->GetViewer()->SetEnvironmentSync(true);
            }
            else if( cmd == "stop" ) {
                GetEnv()->GetViewer()->SetEnvironmentSync(false);
            }
            else {
                RAVELOG_WARNA("unknown render command: %s\n", cmd.c_str());
            }

            if( is->fail() || !*is )
                break;
        }

        return true;
    }

    /// orEnvSetOptions - Set physics simulation parameters,
    bool orEnvSetOptions(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        string cmd;
        is >> cmd;
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
        
        if( cmd == "quit" ) {
            GetEnv()->Reset();
            // call exit in a different thread
            new boost::thread(boost::bind(exit,0));
        }
        return true;
    }

    bool worSetOptions(boost::shared_ptr<istream> is, boost::shared_ptr<void> pdata)
    {
        string cmd;
        while(1) {
            *is >> cmd;
            if( !*is )
                break;
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            if( cmd == "physics" ) {
                string name;
                *is >> name;
                if( !*is  || name.size() == 0 ) {
                    RAVELOG_DEBUGA("resetting physics engine\n");
                    GetEnv()->SetPhysicsEngine(PhysicsEngineBasePtr());
                }
                else {
                    PhysicsEngineBasePtr pnewengine = GetEnv()->CreatePhysicsEngine(name);
                    
                    if( !!pnewengine ) {
                        RAVELOG_DEBUGA("setting physics engine to %s\n",name.c_str());
                        GetEnv()->SetPhysicsEngine(pnewengine);
                    }
                }
            }
            else if( cmd == "collision" ) {
                string name;
                *is >> name;
                if( !*is  || name.size() == 0 ) {
                    RAVELOG_DEBUGA("resetting collision checker\n");
                    GetEnv()->SetCollisionChecker(CollisionCheckerBasePtr());
                }
                else {
                    CollisionCheckerBasePtr p = GetEnv()->CreateCollisionChecker(name);
                    
                    if( !!p ) {
                        RAVELOG_DEBUGA("setting collision checker to %s\n",name.c_str());
                        GetEnv()->SetCollisionChecker(p);
                    }
                }
            }
            else if( cmd == "simulation" ) {
                string simcmd;
                *is >> simcmd;
                std::transform(simcmd.begin(), simcmd.end(), simcmd.begin(), ::tolower);

                if( simcmd == "start" || simcmd == "on" ) {
                    dReal fdeltatime = 0.01f;
                    *is >> fdeltatime;                    
                    RAVELOG_DEBUGA("starting simulation loop, timestep=%f\n", (float)fdeltatime);
                    GetEnv()->StartSimulation(fdeltatime);
                }
                else {
                    RAVELOG_DEBUGA("stopping simulation loop\n");
                    GetEnv()->StopSimulation();
                }
            }
            else if( cmd == "debug" ) {
                int level = GetEnv()->GetDebugLevel();
                *is >> level;
                GetEnv()->SetDebugLevel((DebugLevel)level);
            }
            else if( cmd == "gravity" ) {
                Vector vgravity;
                *is >> vgravity.x >> vgravity.y >> vgravity.z;
                if( !!*is ) {
                    RAVELOG_DEBUGA("set gravity (%f,%f,%f)\n", vgravity.x,vgravity.y,vgravity.z);
                    GetEnv()->GetPhysicsEngine()->SetGravity(vgravity);
                }
            }
            else if( cmd == "quit" ) {
                //GetEnv()->Reset();
                exit(0);
            }
            else if( cmd == "selfcollision" ) {
                string newcmd;
                *is >> newcmd;
                std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

                if( newcmd == "on" ) {
                    GetEnv()->GetPhysicsEngine()->SetPhysicsOptions(OpenRAVE::PEO_SelfCollisions);
                    RAVELOG_DEBUGA("set self collisions to on\n");
                }
                else {
                    GetEnv()->GetPhysicsEngine()->SetPhysicsOptions(GetEnv()->GetPhysicsEngine()->GetPhysicsOptions()&~OpenRAVE::PEO_SelfCollisions);
                    RAVELOG_DEBUGA("set self collisions to off\n");
                }
            }

            if( is->eof() || !*is )
                break;
        }

        return true;
    }

    /// orEnvSetOptions - Set physics simulation parameters,
    bool orEnvLoadScene(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        bool bClearScene=false;
        string filename;
        is >> filename >> bClearScene;
        if( !is || filename.size() == 0 ) {
            RAVELOG_DEBUGA("resetting scene\n");
            _mapProblems.clear();
            GetEnv()->Reset();
            return true;
        }
        else {
            if( bClearScene ) {
                RAVELOG_VERBOSEA("resetting scene\n");
                GetEnv()->Reset();
                _mapProblems.clear();
                RAVELOG_VERBOSEA("resetting destroying\n");
            }

            RAVELOG_DEBUGA("Loading scene %s\n", filename.c_str());
            return GetEnv()->Load(filename);
        }

        return true;
    }

    /// robot = orEnvCreateRobot(name, xmlfile) - create a specific robot, return a robot handle (a robot is also a kinbody)
    bool orEnvCreateRobot(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        string robotname, xmlfile, robottype;
        is >> robotname >> xmlfile >> robottype;
        if( !is )
            return false;

        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr robot = GetEnv()->CreateRobot(robottype);
        if( !robot )
            return false;
        if( !robot->InitFromFile(xmlfile, list<pair<string,string> >()) )
            return false;
    
        robot->SetName(robotname);
        if( !GetEnv()->AddRobot(robot) ) {
            RAVELOG_WARNA("failed to add robot");
            return false;
        }

        os << robot->GetEnvironmentId();
        return true;
    }

    bool orEnvCreateProblem(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        string problemname;
        bool bDestroyDuplicates = true;
        is >> bDestroyDuplicates >> problemname;
        if( !is )
            return false;
        
        std::string strargs((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        SyncWithWorkerThread();
    
        if( bDestroyDuplicates ) {
            // if there's a duplicate problem instance, delete it
            map<int, ProblemInstancePtr >::iterator itprob = _mapProblems.begin();
            while(itprob != _mapProblems.end()) {
                if( itprob->second->GetXMLId() == problemname ) {
                    RAVELOG_DEBUGA("deleting duplicate problem %s\n", problemname.c_str());
                    if( !GetEnv()->RemoveProblem(itprob->second) )
                        RAVELOG_WARNA("environment failed to remove duplicate problem %s\n", problemname.c_str());
                    _mapProblems.erase(itprob++);
                }
                else ++itprob;
            }
        }
    
        ProblemInstancePtr prob = GetEnv()->CreateProblem(problemname);
        if( !prob ) {
            RAVELOG_ERRORA("Cannot find probleminstance: %s\n", problemname.c_str());
            return false;
        }
    
        pdata.reset(new pair<ProblemInstancePtr,string>(prob,strargs));
        _mapProblems[_nIdIndex] = prob;
        os << _nIdIndex++;
        return true;
    }

    bool worEnvCreateProblem(boost::shared_ptr<istream> is, boost::shared_ptr<void> pdata)
    {
        if( GetEnv()->LoadProblem(boost::static_pointer_cast< pair<ProblemInstancePtr,string> >(pdata)->first, boost::static_pointer_cast< pair<ProblemInstancePtr,string> >(pdata)->second) != 0 ) {
            RAVELOG_WARNA("failed to load problem");
            return false;
        }
        return true;
    }

    bool worEnvDestroyProblem(boost::shared_ptr<istream> is, boost::shared_ptr<void> pdata)
    {
        int index = 0;
        *is >> index;
        if( !*is )
            return false;
        map<int, ProblemInstancePtr >::iterator it = _mapProblems.find(index);
        if( it != _mapProblems.end() ) {
            if( !GetEnv()->RemoveProblem(it->second) )
                RAVELOG_WARNA("orEnvDestroyProblem: failed to remove problem from environment\n");
            _mapProblems.erase(it);
        }
        else
            RAVELOG_WARNA("orEnvDestroyProblem: cannot find problem with id %d\n", index);

        return true;
    }

    /// body = orEnvCreateKinBody(name, xmlfile) - create a specific kinbody
    bool orEnvCreateKinBody(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        string bodyname, xmlfile;
        is >> bodyname >> xmlfile;
        if( !is ) {
            return false;
        }

        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr body = GetEnv()->ReadKinBodyXMLFile(KinBodyPtr(),xmlfile,list<pair<string,string> >());

        if( !body ) {
            return false;
        }
        body->SetName(bodyname);

        if( !GetEnv()->AddKinBody(body) ) {
            return false;
        }

        os << body->GetEnvironmentId();
        return true;
    }

    // bodyid = orEnvGetBody(bodyname)
    // Returns the id of the body given its name
    bool orEnvGetBody(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        string bodyname;
        is >> bodyname;
        if( !is )
            return false;

        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());

        KinBodyPtr pbody = GetEnv()->GetKinBody(bodyname);
        if( !pbody )
            os << "0";
        else
            os << pbody->GetEnvironmentId();
        return true;
    }

    bool orEnvGetRobots(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());

        vector<RobotBasePtr> vrobots;
        GetEnv()->GetRobots(vrobots);

        os << vrobots.size() << " ";
        FOREACHC(it, vrobots)
            os << (*it)->GetEnvironmentId() << " " << (*it)->GetName() << " " << (*it)->GetXMLId() << " " << (*it)->GetXMLFilename() << "\n ";

        return true;
    }

    bool orEnvGetBodies(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());

        vector<KinBodyPtr> vbodies;
        GetEnv()->GetBodies(vbodies);
        os << vbodies.size() << " ";
        FOREACHC(it, vbodies) {
            os << (*it)->GetEnvironmentId() << " " << (*it)->GetName() << " " << (*it)->GetXMLId() << " " << (*it)->GetXMLFilename() << "\n ";
        }

        return true;
    }

    /// values = orBodySetTransform(body, position, rotation) - returns the dof values of a kinbody
    bool orKinBodySetTransform(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        KinBodyPtr pbody = orMacroGetBody(is);
        vector<dReal> values = vector<dReal>((istream_iterator<dReal>(is)), istream_iterator<dReal>());

        Transform t;
        if( values.size() == 7 ) {
            // quaternion and translation
            t.rot.x = values[0];
            t.rot.y = values[1];
            t.rot.z = values[2];
            t.rot.w = values[3];
            t.trans.x = values[4];
            t.trans.y = values[5];
            t.trans.z = values[6];
        }
        else if( values.size() == 12 ) {
            // rotation mat and translation
            TransformMatrix tm;
            tm.m[0] = values[0];    tm.m[1] = values[3];        tm.m[2] = values[6];
            tm.m[4] = values[1];    tm.m[5] = values[4];        tm.m[6] = values[7];
            tm.m[8] = values[2];    tm.m[9] = values[5];        tm.m[10] = values[8];
            tm.trans.x = values[9];
            tm.trans.y = values[10];
            tm.trans.z = values[11];
            t = Transform(tm);
        }
        else if( values.size() == 3 ) {
            // just translation, rotation is identity
            t.trans.x = values[0];
            t.trans.y = values[1];
            t.trans.z = values[2];
        }
        else
            return false;

        // normalize the rotation first
        normalize4(t.rot, t.rot);

        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        pbody->SetTransform(t);
        
        if( pbody->IsRobot() ) {
            RobotBasePtr probot = boost::static_pointer_cast<RobotBase>(pbody);
            ControllerBasePtr pcontroller = probot->GetController();
            if( !!pcontroller )
                // if robot, reset the trajectory
                pcontroller->Reset(0);
        }

        return true;
    }

    /// orRobotSetActiveDOFs(robot, indices, affinedofs, axis) - returns the dof values of a kinbody
    bool orBodyDestroy(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        KinBodyPtr pbody = orMacroGetBody(is);
        if( !pbody )
            return false;
        return GetEnv()->RemoveKinBody(pbody);
    }

    bool orBodyEnable(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        KinBodyPtr pbody = orMacroGetBody(is);
        if( !pbody )
            return false;

        bool bEnable = true;
        is >> bEnable;
        if( !is )
            return false;

        pbody->Enable(bEnable);
        return true;
    }

    /// values = orBodyGetLinks(body) - returns the dof values of a kinbody
    bool orBodyGetLinks(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr body = orMacroGetBody(is);
        if( !body )
            return false;

        vector<Transform> trans;
        body->GetBodyTransformations(trans);
        FOREACHC(it, trans)
            os << TransformMatrix(*it) << " ";
        return true;
    }

    bool orRobotControllerSend(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot || !probot->GetController() )
            return false;

        // the next word should be the command
        if( probot->GetController()->SendCommand(os,is) )
            return true;
        return false;
    }

    bool orRobotSensorSend(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;

        int sensorindex = 0;
        is >> sensorindex;
        if( !is )
            return false;
        
        if( sensorindex < 0 || sensorindex >= (int)probot->GetSensors().size() )
            return false;
        return probot->GetSensors()[sensorindex]->GetSensor()->SendCommand(os,is);
    };

    bool orRobotSensorData(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;

        int sensorindex = 0, options = 0;
        is >> sensorindex >> options;
        if( !is )
            return false;
        if( sensorindex < 0 || sensorindex >= (int)probot->GetSensors().size() )
            return false;

        SensorBasePtr psensor = probot->GetSensors()[sensorindex]->GetSensor();
        boost::shared_ptr<SensorBase::SensorData> psensordata = psensor->CreateSensorData();

        if( !psensordata ) {
            RAVELOG_ERRORA("Robot %s, failed to create sensor %s data\n", probot->GetName().c_str(), probot->GetSensors()[sensorindex]->GetName().c_str());
            return false;
        }

        if( !psensor->GetSensorData(psensordata) ) {
            RAVELOG_ERRORA("Robot %s, failed to get sensor %s data\n", probot->GetName().c_str(), probot->GetSensors()[sensorindex]->GetName().c_str());
            return false;
        }

        // serialize the data
        os << psensordata->GetType() << " ";

        switch(psensordata->GetType()) {
        case SensorBase::ST_Laser: {
            boost::shared_ptr<SensorBase::LaserSensorData> plaserdata = boost::static_pointer_cast<SensorBase::LaserSensorData>(psensordata);
            os << plaserdata->ranges.size() << " ";
            if( plaserdata->positions.size() != plaserdata->ranges.size() )
                os << "1 ";
            else
                os << plaserdata->positions.size() << " ";
        
            if( options & 1 )
                os << plaserdata->intensity.size() << " ";
            else
                os << "0 "; // don't send any intensity data

            FOREACH(it, plaserdata->ranges)
                os << it->x << " " << it->y << " " << it->z << " ";
            if( plaserdata->positions.size() != plaserdata->ranges.size() )
                os << plaserdata->t.trans.x << " " << plaserdata->t.trans.y << " " << plaserdata->t.trans.z << " ";
        
            if( options & 1 ) {
                FOREACH(it, plaserdata->intensity)
                    os << *it << " ";
            }

            break;
        }
        case SensorBase::ST_Camera: {
            boost::shared_ptr<SensorBase::CameraSensorData> pcameradata = boost::static_pointer_cast<SensorBase::CameraSensorData>(psensordata);

            if( psensor->GetSensorGeometry()->GetType() != SensorBase::ST_Camera ) {
                RAVELOG_ERRORA("sensor geometry not a camera type\n");
                return false;
            }

            boost::shared_ptr<SensorBase::CameraGeomData> pgeom = boost::static_pointer_cast<SensorBase::CameraGeomData>(psensor->GetSensorGeometry());

            if( (int)pcameradata->vimagedata.size() != pgeom->width*pgeom->height*3 ) {
                RAVELOG_ERRORA(str(boost::format("image data wrong size %d != %d\n")%pcameradata->vimagedata.size()%(pgeom->width*pgeom->height*3)));
                return false;
            }

            os << pgeom->width << " " << pgeom->height << " " << pgeom->KK.fx << " " << pgeom->KK.fy << " " << pgeom->KK.cx << " " << pgeom->KK.cy << " " << TransformMatrix(pcameradata->t) << " ";

            // RLE encoding (about 3x faster than sending raw images)
            int curvalue = 0, lastdiff = 0, lastvalue = 0xffffff&*(int*)&pcameradata->vimagedata[0];
            list<int> difs, values;
            for(int i = 1; i < (int)pcameradata->vimagedata.size()/3; ++i) {
                curvalue = 0xffffff&*(int*)&pcameradata->vimagedata[3*i];
                if( curvalue != lastvalue ) {
                    values.push_back(lastvalue);
                    difs.push_back(i-lastdiff);
                    lastdiff = i;
                    lastvalue = curvalue;
                }
            }
            difs.push_back(pcameradata->vimagedata.size()/3-lastdiff);
            values.push_back(curvalue);

            os << values.size() << " ";
            FOREACH(it, values)
                os << *it << " ";
            os << difs.size() << " ";
            FOREACH(it, difs)
                os << *it << " ";
            break;
        }
        case SensorBase::ST_JointEncoder:
        case SensorBase::ST_Force6D:
        default:
            RAVELOG_WARNA("sensor type %d not supported\n", psensordata->GetType());
            break;
        }
    
        return true;
    }

    bool orRobotControllerSet(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;

        string controllername;
        is >> controllername;
        if( !is )
            return false;
        ControllerBasePtr pcontroller = GetEnv()->CreateController(controllername);
        if( !pcontroller )
            return false;

        std::string strargs((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        return probot->SetController(pcontroller, strargs);
    }

    bool orEnvClose(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        vector<int> ids = vector<int>((istream_iterator<int>(is)), istream_iterator<int>());
        if( ids.size() == 0 ) {
            _mapFigureIds.clear();
        }
        else {
            FOREACH(itid,ids)
                _mapFigureIds.erase(*itid);
        }
        return true;
    }

    bool orEnvPlot(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        int id = _nNextFigureId++;

        vector<RaveVector<float> > vpoints;
        vector<float> vcolors;

        int numpoints=0, numcolors=0;
        float fsize=0;
        int drawstyle = 0;
        float ftransparency=0;

        is >> numpoints;
        vpoints.reserve(numpoints);

        for(int i = 0; i < numpoints; ++i) {
            Vector v;
            is >> v.x >> v.y >> v.z;
            vpoints.push_back(v);
        }

        is >> numcolors;
        vcolors.resize(3*numcolors);

        for(int i = 0; i < numcolors*3; ++i)
            is >> vcolors[i];
    
        if( vcolors.size() == 0 ) {
            vcolors.push_back(1);
            vcolors.push_back(0.5f);
            vcolors.push_back(0.5f);
        }

        is >> fsize >> drawstyle >> ftransparency;
        float falpha = 1-ftransparency;

        if( !is ) {
            RAVELOG_ERRORA("error occured in orEnvPlot stream\n");
            return false;
        }

        if( numcolors > 1 && numcolors != numpoints) {
            RAVELOG_WARNA(str(boost::format("number of colors (%d) != number of points (%d)\n")%vcolors.size()%numpoints));
            numcolors = 1;
        }

        EnvironmentBase::GraphHandlePtr figure;

        switch(drawstyle) {
        case 0: // regular points
            if( numcolors != numpoints ) 
                figure = GetEnv()->plot3(&vpoints[0].x,vpoints.size(),sizeof(vpoints[0]),fsize,
                                         RaveVector<float>(vcolors[0], vcolors[1], vcolors[2], falpha),0);
            else
                figure = GetEnv()->plot3(&vpoints[0].x,vpoints.size(),sizeof(vpoints[0]),fsize,&vcolors[0],0);
            break;
        case 1: // line strip
            if( numcolors != numpoints ) 
                figure = GetEnv()->drawlinestrip(&vpoints[0].x,vpoints.size(),sizeof(vpoints[0]),fsize,
                                                 RaveVector<float>(vcolors[0], vcolors[1], vcolors[2], falpha));
            else
                figure = GetEnv()->drawlinestrip(&vpoints[0].x,vpoints.size(),sizeof(vpoints[0]),fsize,&vcolors[0]);
            break;
        case 2: // list lists
            if( numcolors != numpoints ) 
                figure = GetEnv()->drawlinelist(&vpoints[0].x,vpoints.size(),sizeof(vpoints[0]),fsize,
                                                RaveVector<float>(vcolors[0], vcolors[1], vcolors[2], falpha));
            else
                figure = GetEnv()->drawlinelist(&vpoints[0].x,vpoints.size(),sizeof(vpoints[0]),fsize,&vcolors[0]);
            break;
        case 3: // spheres
            if( numcolors != numpoints ) 
                figure = GetEnv()->plot3(&vpoints[0].x,vpoints.size(),sizeof(vpoints[0]),fsize,
                                         RaveVector<float>(vcolors[0], vcolors[1], vcolors[2], falpha),1);
            else
                figure = GetEnv()->plot3(&vpoints[0].x,vpoints.size(),sizeof(vpoints[0]),fsize,&vcolors[0],1);
            break;
        case 4: // triangle list
            //if( numcolors != numpoints ) 
            figure = GetEnv()->drawtrimesh(&vpoints[0].x,sizeof(vpoints[0]),NULL, vpoints.size()/3,
                                           RaveVector<float>(vcolors[0], vcolors[1], vcolors[2], falpha));
            //else
            //figure = GetEnv()->drawtrimesh(&vpoints[0].x,sizeof(vpoints[0]),vpoints.size(),&vcolors[0]);
            break;  
        }

        if( !!figure )
            _mapFigureIds[id] = figure;
        else
            id = 0;
        os << id;
        return true;
    }

    /// orRobotSetActiveDOFs(robot, indices, affinedofs, axis) - returns the dof values of a kinbody
    bool orRobotSetActiveDOFs(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;

        int numindices=0;
        is >> numindices;
        if( numindices < 0 )
            return false;
        vector<int> vindices; vindices.reserve(numindices);
        for(int i = 0; i < numindices; ++i) {
            int tempindex=-1;
            is >> tempindex;
            if( !is )
                return false;
            if( tempindex < 0 || tempindex >= probot->GetDOF() ) {
                RAVELOG_WARNA("bad degree of freedom\n");
                return false;
            }
            vindices.push_back(tempindex);
        }

        int affinedofs=0;
        is >> affinedofs;
        if( !is )
            return false;

        Vector axis;
        if( affinedofs & RobotBase::DOF_RotationAxis ) {
            is >> axis.x >> axis.y >> axis.z;
            if( !is )
                return false;
        }

        probot->SetActiveDOFs(vindices, affinedofs, axis);
        return true;
    }

    bool orRobotCheckSelfCollision(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr probot = orMacroGetBody(is);
        if( !probot )
            return false;

        boost::shared_ptr<COLLISIONREPORT> preport(new COLLISIONREPORT());
        os << probot->CheckSelfCollision(preport);
        return true;
    }

    /// dofs = orRobotGetActiveDOF(body) - returns the active degrees of freedom of the robot
    bool orRobotGetActiveDOF(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;

        os << probot->GetActiveDOF();
        return true;
    }

    /// dofs = orBodyGetAABB(body) - returns the number of active joints of the body 
    bool orBodyGetAABB(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr pbody = orMacroGetBody(is);
        if( !pbody )
            return false;

        AABB ab = pbody->ComputeAABB();
        os << ab.pos.x << " " << ab.pos.y << " " << ab.pos.z << " " << ab.extents.x << " " << ab.extents.y << " " << ab.extents.z;
        return true;
    }

    /// values = orBodyGetLinks(body) - returns the dof values of a kinbody
    bool orBodyGetAABBs(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr pbody = orMacroGetBody(is);
        if( !pbody )
            return false;

        FOREACHC(itlink, pbody->GetLinks()) {
            AABB ab = (*itlink)->ComputeAABB();
            os << ab.pos.x << " " << ab.pos.y << " " << ab.pos.z << " " << ab.extents.x << " " << ab.extents.y << " " << ab.extents.z << " ";
        }

        return true;
    }

    /// dofs = orBodyGetDOF(body) - returns the number of active joints of the body 
    bool orBodyGetDOF(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr pbody = orMacroGetBody(is);
        if( !pbody )
            return false;

        os << pbody->GetDOF();
        return true;
    }

    /// values = orBodyGetDOFValues(body, indices) - returns the dof values of a kinbody
    bool orBodyGetJointValues(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr pbody = orMacroGetBody(is);
        if( !pbody )
            return false;

        vector<dReal> values;
        vector<int> ids = vector<int>((istream_iterator<int>(is)), istream_iterator<int>());

        if( ids.size() == 0 ) {
            pbody->GetJointValues(values);
            FOREACH(it,values)
                os << *it << " ";
        }
        else {
            pbody->GetJointValues(values);
            values.reserve(ids.size());
            FOREACH(it,ids) {
                if( *it < 0 || *it >= pbody->GetDOF() ) {
                    RAVELOG_ERRORA("orBodyGetJointValues bad index\n");
                    return false;
                }
                os << values[*it] << " ";
            }
        }

        return true;
    }

    /// values = orRobotGetDOFValues(body, indices) - returns the dof values of a kinbody
    bool orRobotGetDOFValues(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;

        vector<dReal> values;
        vector<int> ids = vector<int>((istream_iterator<int>(is)), istream_iterator<int>());

        if( ids.size() == 0 ) {
            probot->GetActiveDOFValues(values);
            FOREACH(it,values)
                os << *it << " ";
        }
        else {
            probot->GetJointValues(values);
            values.reserve(ids.size());
            FOREACH(it,ids) {
                if( *it < 0 || *it >= probot->GetDOF() ) {
                    RAVELOG_ERRORA("orBodyGetJointValues bad index\n");
                    return false;
                }
                os << values[*it] << " ";
            }
        }

        return true;
    }

    /// [lower, upper] = orKinBodyGetDOFLimits(body) - returns the dof limits of a kinbody
    bool orRobotGetDOFLimits(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;

        vector<dReal> lower,upper;
        probot->GetActiveDOFLimits(lower,upper);

        os << lower.size() << " ";
        FOREACH(it, lower)
            os << *it << " ";
        FOREACH(it, upper)
            os << *it << " ";

        return true;
    }

    bool orRobotGetManipulators(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;
    
        os << probot->GetManipulators().size() << " ";
        FOREACHC(itmanip, probot->GetManipulators()) {
            if( !(*itmanip)->GetBase() )
                os << "-1 ";
            else
                os << (*itmanip)->GetBase()->GetIndex() << " ";
            if( !(*itmanip)->GetEndEffector() )
                os << "-1 ";
            else
                os << (*itmanip)->GetEndEffector()->GetIndex() << " ";
            os << TransformMatrix((*itmanip)->GetGraspTransform()) << " ";
            os << (*itmanip)->GetGripperJoints().size() << " ";
            FOREACHC(it, (*itmanip)->GetGripperJoints())
                os << *it << " ";
            os << (*itmanip)->GetArmJoints().size() << " ";
            FOREACHC(it, (*itmanip)->GetArmJoints())
                os << *it << " ";
            os << (*itmanip)->GetClosingDirection().size() << " ";
            FOREACHC(it, (*itmanip)->GetClosingDirection())
                os << *it << " ";
            os << (*itmanip)->GetPalmDirection().x << " " << (*itmanip)->GetPalmDirection().y << " " << (*itmanip)->GetPalmDirection().z << " ";
            os << (*itmanip)->GetName().size() << " " << (*itmanip)->GetName() << " ";
            os << (*itmanip)->GetIKSolverName().size() << " " << (*itmanip)->GetIKSolverName() << " ";
        }

        return true;
    }

    bool orRobotGetAttachedSensors(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;

        os << probot->GetSensors().size() << " ";
        FOREACHC(itsensor, probot->GetSensors()) {
            os << (*itsensor)->GetName().size() << " " << (*itsensor)->GetName() << " ";
        
            if( !(*itsensor)->GetAttachingLink() )
                os << "-1 ";
            else
                os << (*itsensor)->GetAttachingLink()->GetIndex() << " ";

            os << TransformMatrix((*itsensor)->GetRelativeTransform()) << " ";

            if( !(*itsensor)->GetSensor() )
                os << "0 " << TransformMatrix() << " ";
            else
                os << (*itsensor)->GetSensor()->GetXMLId().size() << " "
                   << (*itsensor)->GetSensor()->GetXMLId() << " " 
                   << TransformMatrix((*itsensor)->GetSensor()->GetTransform()) << " ";
        }

        return true;
    }

    /// orBodySetJointValues(body, values, indices)
    bool orBodySetJointValues(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr pbody = orMacroGetBody(is);
        if( !pbody )
            return false;

        int dof = 0;
        is >> dof;
        if( !is || dof <= 0 )
            return false;

        vector<dReal> vvalues(dof);
        vector<int> vindices(dof);

        for(int i = 0; i < dof; ++i)
            is >> vvalues[i];
        if( !is )
            return false;

        bool bUseIndices = false;
        for(int i = 0; i < dof; ++i) {
            is >> vindices[i];
            if( !is ) {
                if( i == 0 )
                    break;
                else {
                    RAVELOG_WARNA("incorrect number of indices, ignoring\n");
                    return false;
                }
            }

            bUseIndices = true;
        }

        if( bUseIndices ) {
            vector<dReal> v;
            pbody->GetJointValues(v);
            vector<dReal>::iterator itvalue = vvalues.begin();
            FOREACH(it,vindices) {
                if( *it < 0 || *it >= pbody->GetDOF() ) {
                    RAVELOG_ERRORA("bad index: %d\n", *it);
                    return false;
                }
                v[*it] = *itvalue++;
            }
            pbody->SetJointValues(v, true);
        }
        else {
            // do not use indices
            if( (int)vvalues.size() != pbody->GetDOF() )
                return false;
            pbody->SetJointValues(vvalues, true);
        }

        if( pbody->IsRobot() ) {
            // if robot, have to turn off any trajectory following
            RobotBasePtr probot = boost::static_pointer_cast<RobotBase>(pbody);
            if( !!probot->GetController() ) {
                // reget the values since they'll go through the joint limits
                probot->GetJointValues(vvalues);
                probot->GetController()->SetDesired(vvalues);
            }
        }

        return true;
    }

    /// orBodySetJointTorques(body, values, indices)
    bool orBodySetJointTorques(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr pbody = orMacroGetBody(is);
        if( !pbody )
            return false;

        int dof = 0;
        bool bAdd=false;
        is >> bAdd >> dof;
        if( !is || dof <= 0 )
            return false;
        if( dof != pbody->GetDOF() )
            return false;

        vector<dReal> vvalues(dof);
        for(int i = 0; i < dof; ++i)
            is >> vvalues[i];
        if( !is )
            return false;

        pbody->SetJointTorques(vvalues, bAdd);
        return true;
    }

    /// orRobotSetDOFValues(body, values, indices)
    bool orRobotSetDOFValues(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(is);
        if( !probot )
            return false;

        int dof = 0;
        is >> dof;
        if( !is || dof <= 0 )
            return false;

        vector<dReal> vvalues(dof);
        vector<int> vindices(dof);

        for(int i = 0; i < dof; ++i)
            is >> vvalues[i];
        if( !is )
            return false;

        bool bUseIndices = false;
        for(int i = 0; i < dof; ++i) {
            is >> vindices[i];
            if( !is ) {
                if( i == 0 )
                    break;
                else {
                    RAVELOG_WARNA("incorrect number of indices, ignoring\n");
                    return false;
                }
            }

            bUseIndices = true;
        }

        if( bUseIndices ) {
            vector<dReal> v;
            probot->GetJointValues(v);
            vector<dReal>::iterator itvalue = vvalues.begin();
            FOREACH(it,vindices) {
                if( *it < 0 || *it >= probot->GetDOF() ) {
                    RAVELOG_ERRORA("bad index: %d\n", *it);
                    return false;
                }
                v[*it] = *itvalue++;
            }
            probot->SetJointValues(v, true);
        }
        else {
            // do not use indices
            if( (int)vvalues.size() != probot->GetActiveDOF() )
                return false;
            probot->SetActiveDOFValues(vvalues, true);
        }

        if( !!probot->GetController() ) {
            // reget the values since they'll go through the joint limits
            probot->GetJointValues(vvalues);
            probot->GetController()->SetDesired(vvalues);
        }
    
        return true;
    }

    /// orRobotStartActiveTrajectory(robot, jointvalues, timestamps, transformations)
    /// - starts a trajectory on the robot with the active degrees of freedom
    bool worRobotStartActiveTrajectory(boost::shared_ptr<istream> is, boost::shared_ptr<void> pdata)
    {
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        RobotBasePtr probot = orMacroGetRobot(*is);
        if( !probot )
            return false;

        int numpoints, havetime, havetrans;
        *is >> numpoints >> havetime >> havetrans;
        if( !*is )
            return false;
    
        Transform trans = probot->GetTransform();
        vector<Trajectory::TPOINT> vpoints(numpoints);
        FOREACH(it, vpoints) {
            it->q.resize(probot->GetActiveDOF());
            it->trans = trans;
            FOREACH(itval, it->q)
                *is >> *itval;
            if( !*is )
                return false;
        }
        if( havetime ) {
            FOREACH(it, vpoints)
                *is >> it->time;
        }

        if( havetrans ) {
            if( havetrans == 1 ) { // 3x4 matrix
                TransformMatrix m;
                FOREACH(it, vpoints) {
                    *is >> m;
                    it->trans = m;
                }
            }
            else { // quaternion and translation
                FOREACH(it, vpoints)
                    *is >> it->trans;
            }
        }

        // add all the points
        TrajectoryBasePtr pfulltraj = GetEnv()->CreateTrajectory(probot->GetDOF());

        if( probot->GetActiveDOF() > 0 ) {
            TrajectoryBasePtr ptraj = GetEnv()->CreateTrajectory(probot->GetActiveDOF());
            FOREACH(it, vpoints)
                ptraj->AddPoint(*it);
            probot->GetFullTrajectoryFromActive(pfulltraj, ptraj, false);
        }
        else {
            Trajectory::TPOINT tp;
            probot->GetJointValues(tp.q);
            FOREACH(it, vpoints) {
                tp.time = it->time;
                tp.trans = it->trans;
                pfulltraj->AddPoint(tp);
            }
        }

        if( !*is )
            return false;

        pfulltraj->CalcTrajTiming(probot, pfulltraj->GetInterpMethod(), !havetime, false);
        probot->SetMotion(pfulltraj);
        return true;
    }

    /// [collision, bodycolliding] = orEnvCheckCollision(body) - returns whether a certain body is colliding with the scene
    bool orEnvCheckCollision(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr pbody = orMacroGetBody(is);
        if( !pbody )
            return false;

        int nexcluded = 0;
        is >> nexcluded;
        vector<KinBodyConstPtr> vignore; vignore.reserve(nexcluded);
        for(int i = 0; i < nexcluded; ++i) {
            int bodyid = 0;
            is >> bodyid;
            if( !is )
                return false;
            if( bodyid ) {
                KinBodyPtr pignore = GetEnv()->GetBodyFromEnvironmentId(bodyid);
                if( !pignore )
                    RAVELOG_WARNA("failed to find body %d",bodyid);
                else
                    vignore.push_back(pignore);
            }
        }
        bool bgetcontacts=false;
        is >> bgetcontacts;

        boost::shared_ptr<COLLISIONREPORT> preport(new COLLISIONREPORT());
        vector<KinBody::LinkConstPtr> empty;
        int oldoptions = GetEnv()->GetCollisionChecker()->GetCollisionOptions();
        GetEnv()->GetCollisionChecker()->SetCollisionOptions(CO_Contacts);
        if( GetEnv()->CheckCollision(KinBodyConstPtr(pbody), vignore, empty,preport)) {
            os << "1 ";
            //RAVELOG_VERBOSEA(str(boost::format("collision %s\n")%preport->__str__()));
        }
        else
            os << "0 ";
        GetEnv()->GetCollisionChecker()->SetCollisionOptions(oldoptions);
        int bodyindex = 0;
        if( !!preport->plink1 && preport->plink1->GetParent() != pbody )
            bodyindex = preport->plink1->GetParent()->GetEnvironmentId();
        if( !!preport->plink2 && preport->plink2->GetParent() != pbody )
            bodyindex = preport->plink2->GetParent()->GetEnvironmentId();

        os << bodyindex << " ";

        if( bgetcontacts ) {
            FOREACH(itc,preport->contacts) {
                os << itc->pos.x << " " << itc->pos.y << " " << itc->pos.z << " " << itc->norm.x << " " << itc->norm.y << " " << itc->norm.z << " " << itc->depth << " ";
            }
        }

        return true;
    }

    /// [collision, info] = orEnvRayCollision(rays) - returns the position and normals where all the rays collide
    /// every ray is 6 dims
    /// collision is a N dim vector that is 0 for non colliding rays and 1 for colliding rays
    /// info is a Nx6 vector where the first 3 columns are position and last 3 are normals
    bool orEnvRayCollision(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        KinBodyPtr pbody = orMacroGetBody(is);

        int oldoptions = GetEnv()->GetCollisionChecker()->GetCollisionOptions();
        GetEnv()->GetCollisionChecker()->SetCollisionOptions(oldoptions|CO_Contacts);

        boost::shared_ptr<COLLISIONREPORT> preport(new COLLISIONREPORT());
        RAY r;
        bool bcollision;
        vector<float> info;
        
        while(!is.eof()) {
            is >> r.pos.x >> r.pos.y >> r.pos.z >> r.dir.x >> r.dir.y >> r.dir.z;
            if( !is || is.fail() )
                break;

            if(!pbody)
                bcollision = GetEnv()->CheckCollision(r, preport);
            else
                bcollision = GetEnv()->CheckCollision(r, KinBodyConstPtr(pbody), preport);

            if(bcollision) {
                BOOST_ASSERT(preport->contacts.size()>0);
                COLLISIONREPORT::CONTACT& c = preport->contacts.front();
                os << "1 ";
                info.push_back(c.pos.x); info.push_back(c.pos.y); info.push_back(c.pos.z);
                info.push_back(c.norm.x); info.push_back(c.norm.y); info.push_back(c.norm.z);
            }
            else {
                os << "0 ";
                for(int i = 0; i < 6; ++i)
                    info.push_back(0);
            }
        }

        GetEnv()->GetCollisionChecker()->SetCollisionOptions(oldoptions);
        FOREACH(it, info)
            os << *it << " ";
        return true;
    }

    bool orEnvStepSimulation(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        dReal timestep=0;
        bool bSync=true;
        is >> timestep >> bSync;
        if( bSync ) {
            SyncWithWorkerThread();
            EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
            GetEnv()->StepSimulation(timestep);
        }
        return true;
    }

    bool worEnvStepSimulation(boost::shared_ptr<istream> is, boost::shared_ptr<void> pdata)
    {
        dReal timestep=0;
        bool bSync=true;
        *is >> timestep >> bSync;
        if( !bSync ) {
            GetEnv()->StepSimulation(timestep);
        }
        return true;
    }

    bool orEnvTriangulate(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();

        int inclusive=0;
        is >> inclusive;
        vector<int> vobjids = vector<int>((istream_iterator<int>(is)), istream_iterator<int>());

        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());

        vector<KinBodyPtr> vbodies;
        GetEnv()->GetBodies(vbodies);

        KinBody::Link::TRIMESH trimesh;
        FOREACH(itbody, vbodies) {
            if( (find(vobjids.begin(),vobjids.end(),(*itbody)->GetEnvironmentId()) == vobjids.end()) ^ !inclusive )
                continue;
            GetEnv()->Triangulate(trimesh, *itbody);
        }

        BOOST_ASSERT( (trimesh.indices.size()%3) == 0 );
        os << trimesh.vertices.size() << " " << trimesh.indices.size()/3 << " ";
        FOREACH(itvert, trimesh.vertices)
            os << itvert->x << " " << itvert->y << " " << itvert->z << " ";
        FOREACH(itind, trimesh.indices)
            os << *itind << " ";

        return true;
    }

    // waits for rave to finish commands
    // if a robot id is specified, also waits for that robot's trajectory to finish
    bool orEnvWait(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        SyncWithWorkerThread();
        RobotBasePtr probot;
        ControllerBasePtr pcontroller;
        int timeout = -1;
        float ftimeout;

        {
            EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
            probot = orMacroGetRobot(is);
            if( !probot ) {
                os << "1";
                return true;
            }

            is >> ftimeout;
            if( !!is )
                timeout = (int)(1000*ftimeout);

            pcontroller = probot->GetController();
        }

        if( !!pcontroller ) {
            while( !pcontroller->IsDone() ) {
                Sleep(1);
                if( timeout > 0 ) {
                    if( --timeout == 0 )
                        break;
                }
                if( bCloseThread )
                    return false;
            }

            if( timeout != 0 ) // only ret success
                os << "1";
            else
                os << "0";
        }
        else
            os << "1";

        return true;
    }

    /// sends a comment to the problem
    bool orProblemSendCommand(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        int problemid=0, dosync;
        bool bDoLock;
        is >> problemid >> dosync >> bDoLock;
        if( !is )
            return false;

        SyncWithWorkerThread();
        // do not need lock
        //EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());

        if( problemid > 0 ) {
            map<int, ProblemInstancePtr >::iterator it = _mapProblems.find(problemid);
            if( it == _mapProblems.end() ) {
                RAVELOG_WARNA("failed to find problem %d\n", problemid);
                return false;
            }
            it->second->SendCommand(os,is);
        }
        else {
            stringstream::streampos inputpos = is.tellg();
            list<ProblemInstancePtr> listProblems;
            GetEnv()->GetLoadedProblems(listProblems);
            FOREACHC(itprob, listProblems) {
                is.seekg(inputpos);
                if( !(*itprob)->SendCommand(os,is) ) {
                    RAVELOG_DEBUGA("problem failed");
                    return false;
                }
                os << " ";
            }
        }

        return true;
    }

    /// sends a comment to the problem
    bool orEnvLoadPlugin(istream& is, ostream& os, boost::shared_ptr<void>& pdata)
    {
        string pluginname;
        is >> pluginname;
        if( !is )
            return false;

        return GetEnv()->LoadPlugin(pluginname);
    }

};

#ifdef RAVE_REGISTER_BOOST
#include BOOST_TYPEOF_INCREMENT_REGISTRATION_GROUP()

BOOST_TYPEOF_REGISTER_TYPE(SimpleTextServer::Socket)
BOOST_TYPEOF_REGISTER_TYPE(SimpleTextServer::WORKERSTRUCT)

#endif

#endif