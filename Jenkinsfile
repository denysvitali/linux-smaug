pipeline {
	agent {
		docker {
			image 'ubuntu:bionic'
      label 'docker'
		}
	}
	options {
        timeout(time: 1, unit: 'HOURS')
	}
	stages {
    stage('Prepare Env'){
      steps {
        sh 'apt-get install -y 
            bison
            cdbs
            curl
            dbus-x11
            dpkg-dev
            elfutils
            devscripts
            fakeroot
            flex
            fonts-ipafont
            g++
            git-core
            git-svn
            gperf
            libappindicator-dev
            libappindicator3-dev
            libasound2-dev
            libbrlapi-dev
            libav-tools
            libbz2-dev
            libcairo2-dev
            libcap-dev
            libcups2-dev
            libcurl4-gnutls-dev
            libdrm-dev
            libelf-dev
            libffi-dev
            libgbm-dev
            libglib2.0-dev
            libglu1-mesa-dev
            libgnome-keyring-dev
            libgtk2.0-dev
            libgtk-3-dev
            libkrb5-dev
            libnspr4-dev
            libnss3-dev
            libpam0g-dev
            libpci-dev
            libpulse-dev
            libsctp-dev
            libspeechd-dev
            libsqlite3-dev
            libssl-dev
            libudev-dev
            libwww-perl
            libxslt1-dev
            libxss-dev
            libxt-dev
            libxtst-dev
            locales
            openbox
            p7zip
            patch
            perl
            pkg-config
            python
            python-cherrypy3
            python-crypto
            python-dev
            python-numpy
            python-opencv
            python-openssl
            python-psutil
            python-yaml
            rpm
            ruby
            subversion
            wdiff
            x11-utils
            xcompmgr
            zip'
      }
    }
		stage('Pull') {
			steps {
				sh 'mkdir -p /kernel/linux-smaug && mkdir -p /kernel/kitchen/'
        sh 'ln -s $HOME /kernel/linux-smaug/'
			}
		}
		stage('Compile'){
			steps {
				sh 'cd /kernel/linux-smaug/'
        sh 'export ARCH=arm64'
				sh 'export CROSS_COMPILE=/toolchain/o/bin/aarch64-linux-android-'
				sh './docker-init.sh'
				sh './getvendor.sh -f'
				sh 'yes "" | make dragon_denvit_defconfig'
        sh 'echo "Current dir: " $(pwd)'
				sh 'make -j$(nproc)'
				sh './build-image.sh'
			}
		}
    stage('Archive Artifacts'){
      steps {
        dir('/kernel'){
          sh 'pwd'
          sh 'ls -la /kernel'
          sh 'ls -la /kernel/kitchen/ /kernel/linux-smaug/ /kernel/ramdisk/'
          archiveArtifacts 'linux-smaug/Image.fit,kitchen/*.img'
        }
      }
    }
	}
	post {
		always {
			cleanWs()
		}
	}
}
