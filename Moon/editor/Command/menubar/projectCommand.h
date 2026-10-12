#pragma once
#include "..\command.h"
namespace MOON
{
	/** Starts a project: the scene is taken apart and an empty body is made, so the
	 * first feature has somewhere to go, and the workspace is what is shown. */
	class NewProjectCommand : public Command
	{
	public:
		NewProjectCommand(QObject* parent);
		virtual void execute() override;
	};

	/** Closes the project that is open: the scene is taken apart and the start page
	 * comes back, with the project it was showing kept as one of the recent ones. */
	class CloseProjectCommand : public Command
	{
	public:
		CloseProjectCommand(QObject* parent);
		virtual void execute() override;
	};
}
