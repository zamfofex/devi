devi — a DeviantArt client
===

**devi** uses the DeviantArt RSS APIs to display thumbnail listings.
I wrote this because I was feeling bored.
It works reasonably well, though.
Not many features are supported.

usage
---

**dependencies**:
- TLSe (Git submodule)
  - `libtomcrypt` (external)
  - `libtommath` (external)
- sxml (Git submodule)

“External” dependencies must already be installed on the system, whereas “Git submodule” dependencies can be used from this repository’s Git submodules.

~~~ shell
git clone --recursive https://github.com/zamfofex/devi
cd devi
make
./devi
# then browse to <http://localhost:8017>
~~~

Note that as an alternative approach, TLSe allows `libtomcrypt` and `libtommath` to be unavailable on the system by compiling as an amalgamation.
Run `make LIBS= CPPFLAGS=-DTLS_AMALGAMATION` to employ that techinique.

screenshots
---

(No screenshots yet!)

license
---

**devi** by zamfofex, October of 2021

Copyleft: This is a free work, you can copy, distribute, and modify it under the terms of the Free Art License <https://artlibre.org/licence/lal/en/>
