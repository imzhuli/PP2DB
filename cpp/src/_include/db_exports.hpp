#include <core/core_min.hpp>
#include <string>

namespace pp2db {

	struct xdbStaticIpAccount {
		std::string AccountName;
		std::string AccountPassword;
		std::string TargetIpAddress;
		bool        EnableUdp;
		bool        EnableIpv6;
	};

	X_EXTERN bool InitPP2DB(const char * DbUser, const char * DbPass, const char * DbName, const char * DbHost, uint16_t Port, int Threads);
	X_EXTERN void CleanPP2DB();
	X_EXTERN void PoolPP2DBResults();

	struct xResultBase {};
	using xCallbackContext     = xel::xVariable;
	using xQueryResultCallback = void (*)(xCallbackContext, const xResultBase * ResultPtr);

	struct xQueryStaticIpResult : public xResultBase {
		std::string ExportIp;
		bool        EnableUdp;
	};
	X_EXTERN void PostQueryStaticIp(xQueryResultCallback, xCallbackContext, const std::string & Account, const std::string & Password);

}  // namespace pp2db
