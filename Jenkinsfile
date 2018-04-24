pipeline {
	agent {
		docker {
			image 'dvitali/android-build:feb-5-2'
			args '-v $HOME/build:/kernel --privileged'
		}
	}
	stages {
		stage('Pull') {
			steps {
				sh 'mkdir -p /kernel/linux-smaug && cd /kernel/linux-smaug'
				checkout scm
			}
		}
		stage('Compile'){
				sh 'cd /kernel/linux-smaug/'
				sh './docker-init.sh'
				sh './get-vendor.sh'
				sh 'make -j$(nproc)'
				sh './build-image.sh'
		}
	}
}
