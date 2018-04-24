pipeline {
	agent {
		docker {
			image 'dvitali/android-build:feb-20'
			args '-v $HOME/build:/kernel --privileged'
		}
	}
	options {
    		skipDefaultCheckout(true)
	}
	stages {
		stage('Pull') {
			steps {
				sh 'mkdir -p /kernel/linux-smaug && mkdir -p /kernel/kitchen/ && cd /kernel/linux-smaug'
				checkout scm
			}
		}
		stage('Compile'){
			steps {
				sh 'cd /kernel/linux-smaug/'
				sh 'export ARCH=arm64'
				sh 'export CROSS_COMPILE=/toolchain/o/bin/aarch64-linux-android-'
				sh './docker-init.sh'
				sh './getvendor.sh -f'
				sh 'yes "" | make dragon_denvit'
				sh 'make -j$(nproc)'
				sh './build-image.sh'
			}
		}
	}
	post {
		always {
			cleanWs()
		}
	}
}
