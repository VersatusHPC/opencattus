FROM vagrantlibvirt/vagrant-libvirt:latest
ENV VAGRANT_DISABLE_STRICT_DEPENDENCY_ENFORCEMENT=1
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=America/Sao_Paulo
RUN apt update -y && apt install ansible -y
RUN vagrant plugin install vagrant-scp
