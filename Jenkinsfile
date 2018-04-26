pipeline {
	agent {
		docker {
			image 'dvitali/android-build:feb-20'
      label 'docker'
		}
	}
	options {
        timeout(time: 1, unit: 'HOURS')
		    retry(3)
	}
	stages {
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
