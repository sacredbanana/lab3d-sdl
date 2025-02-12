# Base image with cross-building support
FROM --platform=$BUILDPLATFORM tonistiigi/xx AS xx

# Builder stage with Ubuntu and required tools. Anything newer than Mantic (23.10) gets conflicting signed-by GPG issues.
FROM --platform=$BUILDPLATFORM ubuntu:25.04 AS builder

# Copy xx scripts to the build stage
COPY --from=xx / /

# Prepare apt and install basic tools
RUN apt-get update && apt-get install -y clang lld cmake

# Export TARGETPLATFORM and other necessary ARGs
ARG TARGETPLATFORM

# Set up environment for cross-building
WORKDIR /work

# Update and install dependencies with architecture considerations
RUN xx-apt-get install -y libstdc++-11-dev libglu1-mesa libglu1-mesa-dev libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-image-dev

# Copy the source files
COPY . .

# Configure and build the project
RUN cd build && rm -rf * && \
    cmake $(xx-clang --print-cmake-defines) .. && \
    make

# Prepare the final stage
RUN cd /work && \
    mv dist/linux ken

# Package the application
RUN tar -czf kens-labyrinth-$(xx-info os)-$(xx-info arch)$(xx-info variant).tar.gz -C ken . && \
    mv *.tar.gz ken/

# Minimal final image
FROM scratch AS ken
COPY --from=builder /work/ken /
ENTRYPOINT ["/"]