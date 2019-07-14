# DO NOT EDIT
# This makefile makes sure all linkable targets are
# up-to-date with anything they link to
default:
	echo "Do not invoke directly"

# Rules to remove targets that are older than anything to which they
# link.  This forces Xcode to relink the targets from scratch.  It
# does not seem to check these dependencies itself.
PostBuild.Kens-Labyrinth.Debug:
/Users/cameron/Downloads/lab3d-sdl/macOS/Debug/Kens-Labyrinth.app/Contents/MacOS/Kens-Labyrinth:\
	/usr/lib/libz.dylib
	/bin/rm -f /Users/cameron/Downloads/lab3d-sdl/macOS/Debug/Kens-Labyrinth.app/Contents/MacOS/Kens-Labyrinth


PostBuild.Kens-Labyrinth.Release:
/Users/cameron/Downloads/lab3d-sdl/macOS/Release/Kens-Labyrinth.app/Contents/MacOS/Kens-Labyrinth:\
	/usr/lib/libz.dylib
	/bin/rm -f /Users/cameron/Downloads/lab3d-sdl/macOS/Release/Kens-Labyrinth.app/Contents/MacOS/Kens-Labyrinth


PostBuild.Kens-Labyrinth.MinSizeRel:
/Users/cameron/Downloads/lab3d-sdl/macOS/MinSizeRel/Kens-Labyrinth.app/Contents/MacOS/Kens-Labyrinth:\
	/usr/lib/libz.dylib
	/bin/rm -f /Users/cameron/Downloads/lab3d-sdl/macOS/MinSizeRel/Kens-Labyrinth.app/Contents/MacOS/Kens-Labyrinth


PostBuild.Kens-Labyrinth.RelWithDebInfo:
/Users/cameron/Downloads/lab3d-sdl/macOS/RelWithDebInfo/Kens-Labyrinth.app/Contents/MacOS/Kens-Labyrinth:\
	/usr/lib/libz.dylib
	/bin/rm -f /Users/cameron/Downloads/lab3d-sdl/macOS/RelWithDebInfo/Kens-Labyrinth.app/Contents/MacOS/Kens-Labyrinth




# For each target create a dummy ruleso the target does not have to exist
/usr/lib/libz.dylib:
