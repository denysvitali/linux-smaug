pipeline {
	agent {
		docker {
			image 'dvitali/android-build:feb-20'
			args '-v $HOME/build:/kernel -v /var/jenkins_home/workspace/:/var/jenkins_home/workspace/ --privileged --entrypoint=""'
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
        sh 'ls -la'
        sh 'pwd'
			}
		}
		stage('Compile'){
			steps {
				sh 'cd /kernel/linux-smaug/ && \
        export ARCH=arm64 && \
				export CROSS_COMPILE=/toolchain/o/bin/aarch64-linux-android- && \
				./docker-init.sh && \
				./getvendor.sh -f && \
				yes "" | make dragon_denvit_defconfig && \
        echo "Current dir: " $(pwd) && \
				make -j$(nproc) && \
				./build-image.sh'
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
