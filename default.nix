{ stdenv, mpv, SDL2, glew, libGL, libGLU, python3, youtube-dl, addOpenGLRunpath, python38Packages }:

stdenv.mkDerivation {
	name = "moov";
	src = ./.;

	buildInputs = [
		mpv
		SDL2
		glew
		libGL
		libGLU
		python3
		youtube-dl
		addOpenGLRunpath
		python38Packages.pydbus
		python38Packages.html2text

	];

	nativeBuildInputs = [ addOpenGLRunpath ];

	buildPhase = ''
		make -j17
	'';
	installPhase = ''
		mkdir -p $out/bin
		cp moov $out/bin/
		cp moovpidgin.py $out/bin/moovpidgin
	'';

	postFixup = "addOpenGLRunpath $out/bin/mpv";
}

