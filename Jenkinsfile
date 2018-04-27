node {
  docker.image("dvitali/build-container:latest").inside() {
    try {
  	  stage('Pull') {
          checkout([
            $class: 'GitSCM',
            branches: scm.branches,
            doGenerateSubmoduleConfigurations: scm.doGenerateSubmoduleConfigurations,
            extensions: scm.extensions + [[$class: 'CloneOption', noTags: false, reference: '', shallow: true]],
            submoduleCfg: [],
            userRemoteConfigs: scm.userRemoteConfigs
          ])
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
}
