#pragma once

#include <interface.h>
#include <IDpiManager.h>

class IDpiManagerInternal : public IDpiManager
{
public:
	virtual void InitEngine() = 0;
	virtual void InitFromMainHwnd() = 0;
	virtual void InitClient() = 0;
	virtual void Shutdown() = 0;
};

IDpiManagerInternal* DpiManagerInternal();