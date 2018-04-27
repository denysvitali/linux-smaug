dockerNode(image: "dvitali/build-container:latest") {
  try {
	  stage('Pull') {
        checkout scm
	  		sh 'mkdir -p /kernel/linux-smaug && mkdir -p /kernel/kitchen/'
        sh 'ln -s $HOME /kernel/linux-smaug/'
	  }
	  stage('Compile'){
       withEnv(["ARCH=arm64", "CROSS_COMPILE=/toolchain/o/bin/aarch64-linux-android-"]) {
	  		sh 'cd /kernel/linux-smaug/'
	  		sh './docker-init.sh'
	  		sh './getvendor.sh -f'
	  		sh 'yes "" | make dragon_denvit_defconfig'
        sh 'echo "Current dir: " $(pwd)'
	  		sh 'make -j$(nproc)'
	  		sh './build-image.sh'
        }
	  }
    stage('Archive Artifacts'){
        dir('/kernel'){
          sh 'pwd'
          sh 'ls -la /kernel'
          sh 'ls -la /kernel/kitchen/ /kernel/linux-smaug/ /kernel/ramdisk/'
          archiveArtifacts 'linux-smaug/Image.fit,kitchen/*.img'
        }
    }
  } finally {
			cleanWs()
	}
}
