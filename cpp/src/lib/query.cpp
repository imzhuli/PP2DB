#include "./query.hpp"

#include <core/thread.hpp>
using namespace xel;

static xRunState                RunState;
static std::vector<std::thread> WorkerThreads;

static std::string DbUser;
static std::string DbPass;
static std::string DbName;
static std::string DbHost;
static uint16_t    DbPort;

namespace pp2db {

	enum struct eCmd : uint16_t {
		QUERY_STATIC_IP,
	};
	struct xQueryBase
		: xJobNode
		, xVBase {
		eCmd                        Cmd;
		xQueryResultCallback        CB;
		xCallbackContext            CC;
		virtual const xResultBase * GetResult() const = 0;
	};
	using xResultList = xList<xQueryBase>;

	static xJobQueue   JobQueue;
	static xSpinlock   ResultLock;
	static xResultList ResultList;

	static void WorkerThreadFunc();
	static void PostResult(xQueryBase & RB);

	bool InitPP2DB(const char * DbUser, const char * DbPass, const char * DbName, const char * DbHost, uint16_t DbPort, int Threads) {
		assert(RunState == xRunState());
		RunState.Start();
		::DbUser = DbUser;
		::DbPass = DbPass;
		::DbName = DbName;
		::DbHost = DbHost;
		::DbPort = DbPort;
		// cout << DbUser << endl;
		// cout << DbPass << endl;
		// cout << DbName << endl;
		// cout << DbHost << endl;
		// cout << DbPort << endl;
		for (int i = 0; i < Threads; ++i) {
			WorkerThreads.push_back(std::thread(WorkerThreadFunc));
		}
		return true;
	}

	void CleanPP2DB() {
		assert(RunState);
		JobQueue.PostWakeupN(WorkerThreads.size());
		for (auto & T : WorkerThreads) {
			T.join();
		}
		auto L = xResultList();
		do {  // clear all unprocessed results
			auto G = xSpinlockGuard(ResultLock);
			L.GrabListTail(ResultList);
		} while (false);

		auto UnPostJobList = xJobList();
		JobQueue.GrabJobList(UnPostJobList);

		size_t UnprocessedCounter = 0;
		for (auto & N : L) {
			delete &N;
			++UnprocessedCounter;
		}
		for (auto & N : UnPostJobList) {
			delete &static_cast<xQueryBase &>(N);
			++UnprocessedCounter;
		}
		Touch(UnprocessedCounter);
		X_DEBUG_PRINTF("UnprocessedCounter=%zi", UnprocessedCounter);

		RunState.Finish();
	}

#define IMPLEMENT_GET_RESULT                         \
	const xResultBase * GetResult() const override { \
		return &Result;                              \
	}

	struct xQueryStaticIpJob : xQueryBase {
		std::string          Account;
		std::string          Password;
		xQueryStaticIpResult Result;
		IMPLEMENT_GET_RESULT;
	};

	void PostResult(xQueryBase & RB) {
		auto G = xSpinlockGuard(ResultLock);
		ResultList.GrabTail(RB);
	}

	void PostQueryStaticIp(xQueryResultCallback CB, xCallbackContext CC, const std::string & Account, const std::string & Password) {
		auto JP      = new xQueryStaticIpJob;
		JP->Cmd      = eCmd::QUERY_STATIC_IP;
		JP->Account  = Account;
		JP->Password = Password;
		JP->CB       = CB;
		JP->CC       = CC;
		JobQueue.PostJob(*JP);
	};

	void PoolPP2DBResults() {
		auto L = xResultList();
		do {  // clear all unprocessed results
			auto G = xSpinlockGuard(ResultLock);
			L.GrabListTail(ResultList);
		} while (false);
		for (auto & N : L) {
			auto RP = N.GetResult();
			N.CB(N.CC, RP);
			delete &N;
		}
	}

	void WorkerThreadFunc() {
		auto C = xMySqlConn();
		RuntimeAssert(C.Init(DbUser, DbPass, DbName, DbHost, DbPort));

		auto StmtQueryStaticIp = xMySqlStmt();
		RuntimeAssert(
			StmtQueryStaticIp.Init(C, "select proxy_ip,is_udp from t_static_alloc where account=? and password=? and proxy_status = 0")
		);
		while (auto QP = static_cast<xQueryBase *>(JobQueue.WaitForJob())) {
			switch (QP->Cmd) {
				case eCmd::QUERY_STATIC_IP: {
					auto ProxyIp      = std::string();
					auto EnableUdp    = false;
					auto RealQueryPtr = static_cast<xQueryStaticIpJob *>(QP);
					StmtQueryStaticIp.Execute(std::tie(ProxyIp, EnableUdp), RealQueryPtr->Account, RealQueryPtr->Password);
					RealQueryPtr->Result.ExportIp  = std::move(ProxyIp);
					RealQueryPtr->Result.EnableUdp = EnableUdp;
					X_DEBUG_PRINTF("ResultCount=%zi", StmtQueryStaticIp.GetLastResultRows());
					PostResult(*QP);
					break;
				}
				default: {
					PostResult(*QP);
					break;
				}
			}
		}
		StmtQueryStaticIp.Clean();
		C.Clean();
	}

}  // namespace pp2db
