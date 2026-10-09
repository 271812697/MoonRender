#pragma once
#include "..\command.h"
namespace MOON
{
	/** Makes a body - a chain of features of its own - and makes it the body new
	 * features go into. FreeCAD's Body command, without the origin planes it also
	 * creates there. */
	class NewBodyCommand : public Command
	{
	public:
		NewBodyCommand(QObject* parent);
		virtual void execute() override;
	};
}
