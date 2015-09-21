// Copyright (c) 2014-2015 Josh Blum
// SPDX-License-Identifier: BSL-1.0

#include "EnvironmentEval.hpp"
#include <Pothos/Proxy.hpp>
#include <Pothos/Remote.hpp>
#include <Pothos/System/Logger.hpp>
#include <Poco/URI.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Logger.h>
#include <sstream>

EnvironmentEval::EnvironmentEval(void):
    _failureState(false)
{
    return;
}

EnvironmentEval::~EnvironmentEval(void)
{
    return;
}

void EnvironmentEval::acceptConfig(const QString &zoneName, const Poco::JSON::Object::Ptr &config)
{
    _zoneName = zoneName;
    _config = config;
}

void EnvironmentEval::update(void)
{
    if (this->isFailureState()) _env.reset();

    try
    {
        //env already exists, try test communication
        if (_env)
        {
            _env->findProxy("Pothos/Util/EvalEnvironment");
        }
        //otherwise, make a new env
        else
        {
            _env = this->makeEnvironment();
            auto EvalEnvironment = _env->findProxy("Pothos/Util/EvalEnvironment");
            _eval = EvalEnvironment.callProxy("make");
            _failureState = false;
        }
    }
    catch (const Pothos::Exception &ex)
    {
        //dont report errors if we were already in failure mode
        if (_failureState) return;
        _failureState = true;

        //determine if the remote host is offline or the process just crashed
        const auto hostUri = getHostProcFromConfig(_zoneName, _config).first;
        try
        {
            Pothos::RemoteClient client(hostUri);
            _errorMsg = tr("Remote environment %1 crashed").arg(_zoneName);
        }
        catch(const Pothos::RemoteClientError &)
        {
            _errorMsg = tr("Remote host %1 is offline").arg(QString::fromStdString(hostUri));
        }
        poco_error_f2(Poco::Logger::get("PothosGui.EnvironmentEval.update"), "%s - %s", ex.displayText(), _errorMsg.toStdString());
    }
}

HostProcPair EnvironmentEval::getHostProcFromConfig(const QString &zoneName, const Poco::JSON::Object::Ptr &config)
{
    if (zoneName == "gui") return HostProcPair("gui://[::1]", "gui");

    auto hostUri = config?config->getValue<std::string>("hostUri"):"tcp://[::1]";
    auto processName = config?config->getValue<std::string>("processName"):"";
    return HostProcPair(hostUri, processName);
}

Pothos::ProxyEnvironment::Sptr EnvironmentEval::makeEnvironment(void)
{
    if (_zoneName == "gui") return Pothos::ProxyEnvironment::make("managed");

    const auto hostUri = getHostProcFromConfig(_zoneName, _config).first;

    //connect to the remote host and spawn a server
    auto serverEnv = Pothos::RemoteClient(hostUri).makeEnvironment("managed");
    auto serverHandle = serverEnv->findProxy("Pothos/RemoteServer").callProxy("new", "tcp://[::]", false/*noclose*/);

    //construct the uri for the new server
    auto actualPort = serverHandle.call<std::string>("getActualPort");
    Poco::URI newHostUri(hostUri);
    newHostUri.setPort(std::stoul(actualPort));

    //connect the client environment
    auto client = Pothos::RemoteClient(newHostUri.toString());
    client.holdRef(Pothos::Object(serverHandle));
    auto env = client.makeEnvironment("managed");

    //determine log delivery address
    //FIXME syslog listener doesn't support IPv6, special precautions taken:
    const auto logSource = (not _zoneName.isEmpty())? _zoneName.toStdString() : newHostUri.getHost();
    const auto syslogListenPort = Pothos::System::Logger::startSyslogListener();
    Poco::Net::SocketAddress serverAddr(env->getPeeringAddress(), syslogListenPort);
    if (serverAddr.host().isLoopback()) serverAddr = Poco::Net::SocketAddress("localhost", syslogListenPort);
    if (serverAddr.family() == Poco::Net::IPAddress::IPv6)
    {
        poco_warning_f1(Poco::Logger::get("PothosGui.EnvironmentEval.make"),
            "Log forwarding not supported over IPv6: %s", logSource);
        return env;
    }

    //setup log delivery from the server process
    env->findProxy("Pothos/System/Logger").callVoid("startSyslogForwarding", serverAddr.toString());
    env->findProxy("Pothos/System/Logger").callVoid("forwardStdIoToLogging", logSource);
    serverHandle.callVoid("startSyslogForwarding", serverAddr.toString(), logSource);

    return env;
}
