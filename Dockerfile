FROM --platform=$BUILDPLATFORM tonistiigi/xx AS xx

FROM --platform=$BUILDPLATFORM ubuntu:latest AS builder
# copy xx scripts to your build stage
COPY --from=xx / /

RUN apt-get update && apt-get install -y clang lld cmake

# export TARGETPLATFORM (or other TARGET*)
ARG TARGETPLATFORM
# you can now call xx-* commands

WORKDIR /work

RUN xx-apt-get install -y gcc libstdc++-11-dev libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-image-dev

COPY external external
COPY gamedata gamedata
COPY icons icons
COPY include include
COPY romfs romfs
COPY src src
COPY buildconfig.h.in .
COPY CMakeLists.txt .
COPY ksmmidi.txt .

RUN mkdir build && cd build && \
    cmake $(xx-clang --print-cmake-defines) .. && \
    make

RUN cd /work && mkdir dist && cp build/ken dist && cp build/ken.bmp dist && cp build/ksmmidi.txt dist && cp -r build/gamedata dist
RUN tar -czf kens-labyrinth-$(xx-info os)-$(xx-info arch)$(xx-info variant).tar.gz dist/* && mv *.tar.gz dist/

FROM scratch AS ken
COPY --from=builder /work/dist /
ENTRYPOINT [ "/" ]
