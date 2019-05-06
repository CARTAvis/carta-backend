void setBuildStatus(String message, String state) {
  step([
      $class: "GitHubCommitStatusSetter",
      reposSource: [$class: "ManuallyEnteredRepositorySource", url: "https://github.com/CARTAvis/carta-backend"],
      contextSource: [$class: "ManuallyEnteredCommitContextSource", context: "ci/jenkins/build-status"],
      errorHandlers: [[$class: "ChangingBuildStatusErrorHandler", result: "UNSTABLE"]],
      statusResultSource: [ $class: "ConditionalStatusResultSource", results: [[$class: "AnyBuildResult", message: message, state: state]] ]
  ]);
}

pipeline {
    agent any
    stages {
        stage('Build backend') {
            steps {
                sh "export PATH=/usr/local/bin:$PATH"
                sh "git submodule init && git submodule update"
                dir ('build') {
                 sh "pwd"
                 sh "cp ../../cmake-command.sh ."
                 sh "pwd"
                 sh "./cmake-command.sh"
                 sh "make"
                }
            }
        }
        stage('ICD tests') {
            steps {
                    sh "export PATH=/usr/local/bin:$PATH"
                    sh "pwd && ls -sort"
                    dir ('build') {
                      sh "pwd && ls -sort"
                      sh "cp ../../carta-backend-ICD-test-travis.tar.gz . && tar -xvf carta-backend-ICD-test-travis.tar.gz && cp ../../run.sh ."
                      sh "./run.sh # run carta_backend in the background"
                      sh "lsof -i :3002 # check backend is running"
                      dir ('carta-backend-ICD-test-travis') {
                        dir ('protobuf') {
                        sh "pwd && ls -sort"
                        sh "source ~/emsdk/emsdk_env.sh && git submodule init && git submodule update && git checkout master && npm install && ./build_proto.sh # prepare the tests"
                        }
                        sh "source ~/emsdk/emsdk_env.sh && ./run-travis.sh # run the tests"
                      }
                   }
                 echo "Finished !!"
            }
        }
    }
  post {
    success {
        setBuildStatus("Build succeeded", "SUCCESS");
    }
    failure {
        setBuildStatus("Build failed", "FAILURE");
    }
  }
}
