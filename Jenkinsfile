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
    agent none
    stages {
        stage('Build') {
            parallel {
                stage('CentOS7 build') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        sh "export PATH=/usr/local/bin:$PATH"
                        sh "git submodule update --init --recursive"
                        dir ('build') {
                           sh "rm -rf *"
                           sh "cp ../../cmake-command2.sh ."
                           sh "./cmake-command2.sh"
                           sh "make -j\$(nproc)"
                           echo "Preparing for upcoming ICD tests"
                           sh "rm -rf carta-backend-ICD-test"
                           sh "git clone --depth 1 https://github.com/CARTAvis/carta-backend-ICD-test.git && cp ../../run.sh ."
                           dir ('carta-backend-ICD-test') {
                              sh "git submodule init && git submodule update && yarn install"
                              dir ('protobuf') {
                                 sh "./build_proto.sh"
                              }
                              sh "cp ../../../ws-config.json src/test/config.json"
                           }
                           stash includes: "carta_backend", name: "centos7-1_carta_backend_icd"
                        }
                    }
                    post {
                        success {
                            setBuildStatus("CentOS7 build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("CentOS7 build failed", "FAILURE");
                        }
                    }
                }
                stage('MacOS build') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        sh "export PATH=/usr/local/bin:$PATH"
                        sh "git submodule update --init --recursive"
                        dir ('build') {
                           sh "rm -rf *"
                           sh "cp ../../cmake-command.sh ."
                           sh "./cmake-command.sh"
                           sh "make -j 8"
                           echo "Preparing for upcoming ICD tests"
                           sh "rm -rf carta-backend-ICD-test"
                           sh "git clone --depth 1 https://github.com/CARTAvis/carta-backend-ICD-test.git && cp ../../run.sh ."
                           dir ('carta-backend-ICD-test') {
                              sh "git submodule init && git submodule update && yarn install"
                              dir ('protobuf') {
                                 sh "./build_proto.sh"
                              }
                              sh "cp ../../../ws-config.json src/test/config.json"
                           }
                           stash includes: "carta_backend", name: "macos-1_carta_backend_icd"
                        }
                    }
                    post {
                        success {
                            setBuildStatus("MacOS build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("MacOS build failed", "FAILURE");
                        }
                    }
                }
                stage('Ubuntu build') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        sh "export PATH=/usr/local/bin:$PATH"
                        sh "git submodule update --init --recursive"
                        dir ('build') {
                           sh "rm -rf *"
                           sh "cp ../../cmake-command.sh ."
                           sh "./cmake-command.sh"
                           sh "make -j\$(nproc)"
                           echo "Preparing for upcoming ICD tests"
                           sh "rm -rf carta-backend-ICD-test"
                           sh "git clone --depth 1 https://github.com/CARTAvis/carta-backend-ICD-test.git && cp ../../run.sh ."
                           dir ('carta-backend-ICD-test') {
                              sh "git submodule init && git submodule update && yarn install"
                              dir ('protobuf') {
                                 sh "./build_proto.sh"
                              }
                              sh "cp ../../../ws-config.json src/test/config.json"
                         }
                         stash includes: "carta_backend", name: "ubuntu-1_carta_backend_icd"
                        }
                    }
                    post {
                        success {
                            setBuildStatus("MacOS build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("MacOS build failed", "FAILURE");
                        }
                    }
                }
            }
        }
        stage('Unit Tests') {
            parallel {
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        dir ('build/test') {
                            sh "export PATH=/usr/local/bin:$PATH && ./carta_backend_tests --gtest_output=xml:ubuntu_test_detail.xml"
                        }
                        }
                    }
                    post {
                        always {
                            junit 'build/test/ubuntu_test_detail.xml'
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        dir ('build/test') {
                            sh "export PATH=/usr/local/bin:$PATH && ./carta_backend_tests --gtest_filter=-SpatialProfileTest.*Hdf5*:SpatialProfileTest.*HDF5*:Hdf5AttributesTest.*:Hdf5ImageTest.* --gtest_output=xml:macos_test_detail.xml"
                        }
                        }
                    }
                    post {
                        always {
                            junit 'build/test/macos_test_detail.xml'
                        }
                    }
                }
            }
        }
        stage('ICD tests: session') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                session()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                session()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                session()
                            }
                        }
                        }
                    }
                }    
            }
        }
        stage('ICD tests: file-browser') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                file_browser()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                file_browser()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd" 
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                file_browser()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: animator') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                animator()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                animator()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                animator()
                            }
                        }
                        }
                    }
                }     
            }
        }
        stage('ICD tests: contour') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                contour()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                contour()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                contour()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: region statistics') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                region_statistics()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                region_statistics()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                region_statistics()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: region manipulation') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                region_manipulation()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                region_manipulation()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                region_manipulation()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: cube histogram') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                cube_histogram()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                cube_histogram()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                cube_histogram()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: spatial profiler') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                spatial_profiler()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                spatial_profiler()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                spatial_profiler()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: raster tiles') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                raster_tiles()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                raster_tiles()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                raster_tiles()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: catalog') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                catalog()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                catalog()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                catalog()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: moments') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                moment_tests()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                moment_tests()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                moment_tests()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: resume') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                resume_tests()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                resume_tests()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                resume_tests()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: match') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                match_tests()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                match_tests()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                match_tests()
                            }
                        }
                        }
                    }
                }
            }
        }
        stage('ICD tests: close_file') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                close_file_tests()
                            }
                        }
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                close_file_tests()
                            }
                        }
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                close_file_tests()
                            }
                        }
                        }
                    }
                }
            }
        }
    }
}
def session(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT.test.ts # test 1 of 6" 
             sh "CI=true npm test src/test/ACCESS_CARTA_KNOWN_SESSION.test.ts # test 2 of 6"
             sh "CI=true npm test src/test/ACCESS_CARTA_NO_CLIENT_FEATURE.test.ts # test 3 of 6"
             sh "CI=true npm test src/test/ACCESS_CARTA_SAME_ID_TWICE.test.ts # test 4 of 6"
             sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT_CONCURRENT.test.ts # test 5 of 6"
             sh "CI=true npm test src/test/ACCESS_WEBSOCKET.test.ts # test 6 of 6"
         }
     }
}
def file_browser(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/GET_FILELIST.test.ts # test 1 of 9"
             sh "CI=true npm test src/test/GET_FILELIST_ROOTPATH_CONCURRENT.test.ts # test 2 of 9"
             sh "CI=true npm test src/test/FILETYPE_PARSER.test.ts # test 3 of 9"
             sh "CI=true npm test src/test/FILEINFO_FITS.test.ts # test 4 of 9"
             sh "CI=true npm test src/test/FILEINFO_CASA.test.ts # test 5 of 9"
             sh "CI=true npm test src/test/FILEINFO_HDF5.test.ts # test 6 of 9"
             sh "CI=true npm test src/test/FILEINFO_MIRIAD.test.ts # test 7 of 9"
             sh "CI=true npm test src/test/FILEINFO_FITS_MULTIHDU.test.ts # test 8 of 9"
             sh "CI=true npm test src/test/FILEINFO_EXCEPTIONS.test.ts # test 9 of 9"
         }
     }
}
def animator(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/ANIMATOR_DATA_STREAM.test.ts # test 1 of 4"
             sh "CI=true npm test src/test/ANIMATOR_NAVIGATION.test.ts # test 2 of 4"
             sh "CI=true npm test src/test/ANIMATOR_CONTOUR_MATCH.test.ts # test 3 of 4"
             sh "CI=true npm test src/test/ANIMATOR_CONTOUR.test.ts # test 4 of 4"
        }
    }
}
def contour(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA.test.ts # test 1 of 3"
             sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA_NAN.test.ts # test 2 of 3"
             sh "CI=true npm test src/test/CONTOUR_DATA_STREAM.test.ts # test 3 of 3"
         }
     }
}
def region_statistics(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/REGION_STATISTICS_RECTANGLE.test.ts # test 1 of 3"
             sh "CI=true npm test src/test/REGION_STATISTICS_ELLIPSE.test.ts # test 2 of 3"
             sh "CI=true npm test src/test/REGION_STATISTICS_POLYGON.test.ts # test 3 of 3"
         }
     }
}
def region_manipulation(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/REGION_REGISTER.test.ts # test 1 of 9"
             sh "CI=true npm test src/test/CASA_REGION_INFO.test.ts # test 2 of 9"
             sh "CI=true npm test src/test/CASA_REGION_IMPORT_INTERNAL.test.ts # test 3 of 9"
             sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXPORT.test.ts # test 4 of 9"
             sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXCEPTION.test.ts # test 5 of 9"
             sh "CI=true npm test src/test/CASA_REGION_EXPORT.test.ts # test 6 of 9"
             sh "CI=true npm test src/test/DS9_REGION_EXPORT.test.ts # test 7 of 9"
             sh "CI=true npm test src/test/DS9_REGION_IMPORT_EXCEPTION.test.ts # test 8 of 9"
             sh "CI=true npm test src/test/DS9_REGION_IMPORT_EXPORT.test.ts # test 9 of 9"
         }
     }
}
def cube_histogram(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM.test.ts # test 1 of 3"
             sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM_HDF5.test.ts # test 2 of 3"
             sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM_CANCELLATION.test.ts # test 3 of 3"
         }
     }
}
def spatial_profiler(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE.test.ts # test 1 of 2"
             sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE_NaN.test.ts # test 2 of 2"
         }
     }
}
def raster_tiles(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/CHECK_RASTER_TILE_DATA.test.ts # test 1 of 2"
             sh "CI=true npm test src/test/TILE_DATA_REQUEST.test.ts # test 2 of 2"
         }
     }
}
def catalog(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/CATALOG_GENERAL.test.ts # test 1 of 2"
             sh "CI=true npm test src/test/CATALOG_FITS_VOT.test.ts # test 2 of 2"
         }
     }
}
def moment_tests(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/MOMENTS_GENERATOR_CASA.test.ts # test 1 of 6"
             sh "CI=true npm test src/test/MOMENTS_GENERATOR_EXCEPTION.test.ts # test 2 of 6"
             sh "CI=true npm test src/test/MOMENTS_GENERATOR_FITS.test.ts # test 3 of 6"
             sh "CI=true npm test src/test/MOMENTS_GENERATOR_HDF5.test.ts # test 4 of 6"
             sh "CI=true npm test src/test/MOMENTS_GENERATOR_SAVE.test.ts # test 5 of 6"
             sh "CI=true npm test src/test/MOMENTS_GENERATOR_CANCEL.test.ts # test 6 of 6"
         }
     }
}
def resume_tests(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/RESUME_CATALOG.test.ts # test 1 of 4"
             sh "CI=true npm test src/test/RESUME_CONTOUR.test.ts # test 2 of 4"
             sh "CI=true npm test src/test/RESUME_IMAGE.test.ts # test 3 of 4"
             sh "CI=true npm test src/test/RESUME_REGION.test.ts # test 4 of 4"
         }
     }
}
def match_tests(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/MATCH_SPATIAL.test.ts # test 1 of 2"
             sh "CI=true npm test src/test/MATCH_STATS.test.ts # test 2 of 2"
         }
     }
}
def close_file_tests(){
     script {
         ret = false
         retry(3) {
             if (ret) {
                 sleep(time:30,unit:"SECONDS")
                 echo "Trying again"
             } else {
                 ret = true
             }
             sh "CI=true npm test src/test/CLOSE_FILE_SINGLE.test.ts # test 1 of 5"
             sh "CI=true npm test src/test/CLOSE_FILE_ANIMATION.test.ts # test 2 of 5"
             sh "CI=true npm test src/test/CLOSE_FILE_ERROR.test.ts # test 3 of 5"
             sh "CI=true npm test src/test/CLOSE_FILE_SPECTRAL_PROFILE.test.ts # test 4 of 5"
             sh "CI=true npm test src/test/CLOSE_FILE_TILE.test.ts # test 5 of 5"
         }
     } 
}
