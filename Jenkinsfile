pipeline {
	agent {
		docker {
			image 'dvitali/android-build:feb-5-2'
			args '-v $HOME/build:/kernel --entrypoint=zsh --privileged --rm'
		}
	}
	stages {
		stage('Pull') {
			steps {
				sh 'cd /kernel'
				sh 'git clone https://github.com/denysvitali/linux-smaug -b $GIT_BRANCH linux-smaug'
				sh 'ls -la'
				sh './docker-init.sh'
				sh './get-vendor.sh'
				sh 'make -j$(nproc)'
				sh './build-image.sh'
			}
		}
	}
}
