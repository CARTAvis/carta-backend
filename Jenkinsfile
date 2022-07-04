pipeline {
    agent none
    options {
        preserveStashes() 
    }
    stages {
        stage('Build') {
            parallel {
                stage('Ubuntu 20.04') {
                    agent {
                        label "ubuntu2004-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            sh "uname -a"
                            sh "lsb_release -a"
                            sh "git submodule update --init --recursive"
                            dir ('build') {
                                sh "rm -rf *"
                                sh "cmake .. -Dtest=on -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-O0 -g -fsanitize=address -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address' "
                                sh "make -j 32"
                                sh "./carta_backend --version"
                            }
                        }
                    }
                    post {
                        success {
                            slackSend color: 'good', message: 'Ubuntu backend build success';
                        }
                        failure {
                            slackSend color: 'danger', message: 'Ubuntu backend build failed';
                        }
                    }
                }
                stage('macOS 12') {
                    agent {
                        label "macos12-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            sh "export PATH=/usr/local/bin:$PATH"
                            sh "uname -a"
                            sh "sw_vers"
                            sh "git submodule update --init --recursive"
                            dir ('build') {
                                sh "rm -rf *"
                                sh "cmake .. -Dtest=on -DDevSuppressExternalWarnings=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-O0 -g -fsanitize=address -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address' "
                                sh "make -j 8"
                                sh "./carta_backend --version"
                            }
                        }
                    }
                    post {
                        success {
                            slackSend color: 'good', message: 'macOS backend build success';
                        }
                        failure {
                            slackSend color: 'danger', message: 'macOS backend build failed';
                        }
                    }
                }
                stage('AlmaLinux 8.5') {
                    agent {
                        label "almalinux85-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            sh "uname -a"
                            sh "cat /etc/redhat-release"
                            sh "git submodule update --init --recursive"
                            dir ('build') {
                                sh "rm -rf *"
                                sh "cmake .. -Dtest=on -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-O0 -g -fsanitize=address -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address' "
                                sh "make -j 32"
                                sh "./carta_backend --version"
                            }
                        }
                    }
                    post {
                        success {
                            slackSend color: 'good', message: 'RedHat backend build success';
                        }
                        failure {
                            slackSend color: 'danger', message: 'RedHat backend build success';
                        }
                    }
                }
            }
       }
       stage('Unit Tests') {
           parallel {
                stage('Ubuntu 20.04') {
                    agent {
                        label "ubuntu2004-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            dir ('build/test') {
                                sh "ASAN_OPTIONS=suppressions=${WORKSPACE}/debug/asan/myasan.supp LSAN_OPTIONS=suppressions=${WORKSPACE}/debug/asan/myasan-leaks.supp ASAN_SYMBOLIZER_PATH=llvm-symbolizer ./carta_backend_tests --gtest_output=xml:ubuntu_test_detail.xml"
                            }
                        }
                    }
                    post {
                        always {
                            junit 'build/test/ubuntu_test_detail.xml'
                        }
                        success {
                            slackSend color: 'good', message: 'Ubuntu Unit Tests success';
                        }
                        failure {
                            slackSend color: 'danger', message: 'Ubuntu Unit Tests failure';
                        }
                    }   
                }
                stage('macOS 12') {
                    agent {
                        label "macos12-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            dir ('build/test') {
                                sh "ASAN_OPTIONS=suppressions=${WORKSPACE}/debug/asan/myasan.supp:detect_container_overflow=0 ASAN_SYMBOLIZER_PATH=/opt/homebrew/opt/llvm/bin/llvm-symbolizer ./carta_backend_tests --gtest_output=xml:macos_test_detail.xml"
                            }
                        }
                    }
                    post {
                        always {
                            junit 'build/test/macos_test_detail.xml'
                        }
                        success {
                            slackSend color: 'good', message: 'macOS Unit Tests success';
                        }
                        failure {
                            slackSend color: 'danger', message: 'macOS Unit Tests failure';
                        }   
                    }
                }
                stage('AlmaLinux 8.5') {
                    agent {
                        label "almalinux85-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            dir ('build/test') {
                                sh "ASAN_OPTIONS=suppressions=${WORKSPACE}/debug/asan/myasan.supp LSAN_OPTIONS=suppressions=${WORKSPACE}/debug/asan/myasan-leaks.supp ASAN_SYMBOLIZER_PATH=llvm-symbolizer ./carta_backend_tests --gtest_output=xml:almalinux_test_detail.xml"
                            }
                        }
                    }
                    post {
                        always {
                            junit 'build/test/almalinux_test_detail.xml'
                        }
                        success {
                            slackSend color: 'good', message: 'macOS Unit Tests success';
                        }
                        failure {
                            slackSend color: 'danger', message: 'macOS Unit Tests failure';
                        }
                    }
                }
            }
        }
    }
    post {
        success {
            slackSend color: 'good', message: "Success - ${env.JOB_NAME} ${env.BUILD_NUMBER} ${env.BRANCH_NAME} ${env.GIT_BRANCH} (<${env.RUN_DISPLAY_URL}|open>)";
        }
        failure {
             slackSend color: 'danger', message: "Failed - ${env.JOB_NAME} ${env.BUILD_NUMBER} ${env.BRANCH_NAME} ${env.GIT_BRANCH} (<${env.RUN_DISPLAY_URL}|open>)";
        }
    }
}
