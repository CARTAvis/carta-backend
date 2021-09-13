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
    options {
        preserveStashes() 
    }
    stages {
        stage('Build') {
            parallel {
                stage('Ubuntu 18.04') {
                    agent {
                        label "bionic-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            sh "uname -a"
                            sh "lsb_release -a"
                            sh "git submodule update --init --recursive"
                            dir ('build') {
                                sh "cmake .. -Dtest=on"
                                sh "make -j 16"
                                stash includes: "test/**/*", name: "bionic-unit-tests"
                                stash includes: "carta_backend", name: "bionic-backend"
                            }
                        }
                    }
                    post {
                        success {
                            setBuildStatus("build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("build failed", "FAILURE");
                        }
                    }
                }
                stage('Ubuntu 20.04') {
                    agent {
                        label "focal-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            sh "uname -a"
                            sh "lsb_release -a"
                            sh "git submodule update --init --recursive"
                            dir ('build') {
                                sh "cmake .. -Dtest=on -DCMAKE_CXX_FLAGS='--coverage' -DCMAKE_C_FLAGS='--coverage'"
                                sh "make -j 16"
                                stash includes: "test/**/*", name: "focal-unit-tests"
                                stash includes: "carta_backend", name: "focal-backend"
                            }
                        }
                    }
                    post {
                        success {
                            setBuildStatus("build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("build failed", "FAILURE");
                        }
                    }
                }
                stage('macOS 11') {
                    agent {
                        label "macos11-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            sh "export PATH=/usr/local/bin:$PATH"
                            sh "git submodule update --init --recursive"
                            dir ('build') {
                                sh "rm -rf *"
                                sh "cmake .. -Dtest=on -DEnableAvx=On"
                                sh "make -j 8"
                                stash includes: "test/**/*", name: "macos11-unit-tests"
                                stash includes: "carta_backend", name: "macos11-backend"
                            }
                        }
                    }
                    post {
                        success {
                            setBuildStatus("build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("build failed", "FAILURE");
                        }
                    }
                }
                stage('RHEL7') {
                    agent {
                        label "rhel7-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            sh "uname -a"
                            sh "cat /etc/redhat-release"
                            sh "git submodule update --init --recursive"
                            dir ('build') {
                                sh "source /opt/rh/devtoolset-8/enable && cmake .. -Dtest=on"
                                sh "source /opt/rh/devtoolset-8/enable && make -j 16"
                                stash includes: "test/**/*", name: "rhel7-unit-tests"
                                stash includes: "carta_backend", name: "rhel7-backend"
                            }
                        }
                    }
                    post {
                        success {
                            setBuildStatus("build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("build failed", "FAILURE");
                        }
                    }
                }
                stage('RHEL8') {
                    agent {
                        label "rhel8-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            sh "uname -a"
                            sh "cat /etc/redhat-release"
                            sh "git submodule update --init --recursive"
                            dir ('build') {
                                sh "cmake .. -Dtest=on"
                                sh "make -j 16"
                                stash includes: "test/**/*", name: "rhel8-unit-tests"
                                stash includes: "carta_backend", name: "rhel8-backend"
                            }
                        }
                    }
                    post {
                        success {
                            setBuildStatus("build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("build failed", "FAILURE");
                        }
                    }
                }
            }
       }
       stage('Unit Tests') {
           parallel {
                stage('Ubuntu 18.04') {
                    agent {
                        label "bionic-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            unstash "bionic-unit-tests"
                            dir ('test') {
                                sh "./carta_backend_tests --gtest_output=xml:ubuntu_bionic_test_detail.xml"
                            }
                        }
                    }
                    post {
                        always {
                            junit 'test/ubuntu_bionic_test_detail.xml'
                        }
                    }
                }
                stage('Ubuntu 20.04') {
                    agent {
                        label "focal-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            unstash "focal-unit-tests"
                            dir ('test') {
                                sh "./carta_backend_tests --gtest_output=xml:ubuntu_focal_test_detail.xml"
                            }
                        }
                    }
                    post {
                        always {
                            junit 'test/ubuntu_focal_test_detail.xml'
                        }   
                    }   
                }
                stage('macOS 11') {
                    agent {
                        label "macos11-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            unstash "macos11-unit-tests"
                            dir ('test') {
                                sh "./carta_backend_tests --gtest_output=xml:macos11_test_detail.xml"
                            }
                        }
                    }
                    post {
                        always {
                            junit 'test/macos11_test_detail.xml'
                        }
                    }
                }
                stage('RHEL7') {
                    agent {
                        label "rhel7-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            unstash "rhel7-unit-tests"
                            dir ('test') {
                                sh "./carta_backend_tests --gtest_output=xml:rhel7_test_detail.xml"
                            }
                        }
                    }
                    post {
                        always {
                            junit 'test/rhel7_test_detail.xml'
                        }
                    }
                }
                stage('RHEL8') {
                    agent {
                        label "rhel8-docker-agent"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                            unstash "rhel8-unit-tests"
                            dir ('test') {
                                sh "./carta_backend_tests --gtest_output=xml:rhel8_test_detail.xml"
                            }
                        }
                    }
                    post {
                        always {
                            junit 'test/rhel8_test_detail.xml'
                        }
                    }
                }
            }
        }
        stage('Prepare for ICD tests') {
           parallel {
                stage('Ubuntu 18.04') {
                    agent {
                        label "bionic-docker-agent"
                    }
                    steps {
                        prepare_bionic_ICD()
                    }
                }
                stage('Ubuntu 20.04') {
                    agent {
                        label "focal-docker-agent"
                    }
                    steps {
                        prepare_focal_ICD()
                    }
                }
                stage('macOS 11') {
                    agent {
                        label "macos11-docker-agent"
                    }
                    steps {
                        prepare_macos11_ICD()
                    }
                }
                stage('RHEL7') {
                    agent {
                        label "rhel7-docker-agent"
                    }
                    steps {
                        prepare_rhel7_ICD()
                    }
                }
                stage('RHEL8') {
                    agent {
                        label "rhel8-docker-agent"
                    }
                    steps {
                        prepare_rhel8_ICD()
                    }
                }
            }
        }
        stage('ICD tests') {
            matrix {
                axes {
                    axis {
                        name 'PLATFORM'
                        values 'focal', 'bionic', 'rhel7', 'rhel8', 'macos11'
                    }
                }
                stages {
                    stage('session') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                session()
                            }
                        }
                    }
                    stage('file_browser') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                file_browser()
                            }
                        }
                    }
                    stage('animator') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                animator()
                            }
                        }
                    }
                    stage('contour') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                contour()
                            }
                        }
                    }
                    stage('region_statistics') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                region_statistics()
                            }
                        }
                    }
                    stage('region_manipulation') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                region_manipulation()
                            }
                        }
                    }
                    stage('cube_histogram') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                cube_histogram()
                            }
                        }
                    }
                    stage('spatial_profiler') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                spatial_profiler()
                            }
                        }
                    }
                    stage('raster_tiles') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                raster_tiles()
                            }
                        }
                    }
                    stage('catalog') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                catalog()
                            }
                        }
                    }
                    stage('moment') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                moment()
                            }
                        }
                    }
                    stage('resume') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                resume()
                            }
                        }
                    }
                    stage('match') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                match()
                            }
                        }
                    }
                    stage('close_file') {
                        agent {
                            label "${PLATFORM}-docker-agent"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
                                unstash "${PLATFORM}-backend"
                                sh "./carta_backend /images --top_level_folder /images --port 3112 --omp_threads 8 --debug_no_auth true --no_http true --verbosity=5 &"
                                unstash "${PLATFORM}-ICD"
                                close_file()
                            }
                        }
                    }
                }
            }
        }
    }
}

def prepare_focal_ICD() {
    catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
        sh "git clone --depth 1 https://github.com/CARTAvis/carta-backend-ICD-test.git"
        dir ('carta-backend-ICD-test') {
            sh "sed -i '4 i\\    \"type\": \"module\",' package.json"
            sh "git submodule init && git submodule update && npm install"
            dir ('protobuf') {
                sh "./build_proto.sh"
            }
            dir ('src/test') { 
                sh "perl -p -i -e 's/serverURL/serverURL1/' config.json"
                sh "perl -p -i -e 's/serverURL10/serverURL/' config.json"
                sh "perl -p -i -e 's/3002/3112/' config.json"
            }
        }
        stash includes: "carta-backend-ICD-test/**/*", name: "focal-ICD"
    }
}

def prepare_bionic_ICD() {
    catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
        sh "git clone --depth 1 https://github.com/CARTAvis/carta-backend-ICD-test.git"
        dir ('carta-backend-ICD-test') {
            sh "sed -i '4 i\\    \"type\": \"module\",' package.json"
            sh "git submodule init && git submodule update && npm install"
            dir ('protobuf') {
                sh "./build_proto.sh"
            }
            dir ('src/test') { 
                sh "perl -p -i -e 's/serverURL/serverURL1/' config.json"
                sh "perl -p -i -e 's/serverURL10/serverURL/' config.json"
                sh "perl -p -i -e 's/3002/3112/' config.json"
            }
        }
        stash includes: "carta-backend-ICD-test/**/*", name: "bionic-ICD"
    }
}

def prepare_rhel7_ICD() {
    catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
        sh "git clone --depth 1 https://github.com/CARTAvis/carta-backend-ICD-test.git"
        dir ('carta-backend-ICD-test') {
            sh "sed -i '4 i\\    \"type\": \"module\",' package.json"
            sh "git submodule init && git submodule update && npm install"
            dir ('protobuf') {
                sh "./build_proto.sh"
            }
            dir ('src/test') {
                sh "perl -p -i -e 's/serverURL/serverURL1/' config.json"
                sh "perl -p -i -e 's/serverURL10/serverURL/' config.json"
                sh "perl -p -i -e 's/3002/3112/' config.json"
            }
       }
       stash includes: "carta-backend-ICD-test/**/*", name: "rhel7-ICD"
    }
}

def prepare_rhel8_ICD() {
    catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
        sh "git clone --depth 1 https://github.com/CARTAvis/carta-backend-ICD-test.git"
        dir ('carta-backend-ICD-test') {
            sh "sed -i '4 i\\    \"type\": \"module\",' package.json"
            sh "git submodule init && git submodule update && npm install"
            dir ('protobuf') {
                sh "./build_proto.sh"
            }
            dir ('src/test') {
                sh "perl -p -i -e 's/serverURL/serverURL1/' config.json"
                sh "perl -p -i -e 's/serverURL10/serverURL/' config.json"
                sh "perl -p -i -e 's/3002/3112/' config.json"
            }
       }
       stash includes: "carta-backend-ICD-test/**/*", name: "rhel8-ICD"
    }
}

def prepare_macos11_ICD() {
    catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
        sh "rm -rf carta-backend-ICD-test"
        sh "git clone --depth 1 https://github.com/CARTAvis/carta-backend-ICD-test.git"
        dir ('carta-backend-ICD-test') {
            sh "git submodule init && git submodule update && npm install"
            dir ('protobuf') {
                sh "./build_proto.sh"
            }
            sh "cp ../../config.json src/test/config.json"
        }
        stash includes: "carta-backend-ICD-test/**/*", name: "macos11-ICD"
    }
}

def session() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT.test.ts # test 1 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/ACCESS_CARTA_KNOWN_SESSION.test.ts # test 2 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/ACCESS_CARTA_NO_CLIENT_FEATURE.test.ts # test 3 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/ACCESS_CARTA_SAME_ID_TWICE.test.ts # test 4 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT_CONCURRENT.test.ts # test 5 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/ACCESS_WEBSOCKET.test.ts # test 6 of 6"
                sh "pgrep carta_backend"
            }
        }
    }
}

def file_browser() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/GET_FILELIST.test.ts # test 1 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/GET_FILELIST_ROOTPATH_CONCURRENT.test.ts # test 2 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/FILETYPE_PARSER.test.ts # test 3 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/FILEINFO_FITS.test.ts # test 4 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/FILEINFO_CASA.test.ts # test 5 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/FILEINFO_HDF5.test.ts # test 6 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/FILEINFO_MIRIAD.test.ts # test 7 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/FILEINFO_FITS_MULTIHDU.test.ts # test 8 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/FILEINFO_EXCEPTIONS.test.ts # test 9 of 9"
                sh "pgrep carta_backend"
            }
        }
    }
}

def animator() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/ANIMATOR_DATA_STREAM.test.ts # test 1 of 4"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/ANIMATOR_NAVIGATION.test.ts # test 2 of 4"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/ANIMATOR_CONTOUR_MATCH.test.ts # test 3 of 4"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/ANIMATOR_CONTOUR.test.ts # test 4 of 4"
                sh "pgrep carta_backend"
            }
        }
    }
}

def contour() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA.test.ts # test 1 of 3"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA_NAN.test.ts # test 2 of 2"
            }
        }
    }
}

def region_statistics() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/REGION_STATISTICS_RECTANGLE.test.ts # test 1 of 3"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/REGION_STATISTICS_ELLIPSE.test.ts # test 2 of 3"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/REGION_STATISTICS_POLYGON.test.ts # test 3 of 3"
                sh "pgrep carta_backend"
            }
        }
    }
}

def region_manipulation() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/REGION_REGISTER.test.ts # test 1 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CASA_REGION_INFO.test.ts # test 2 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CASA_REGION_IMPORT_INTERNAL.test.ts # test 3 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXPORT.test.ts # test 4 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXCEPTION.test.ts # test 5 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CASA_REGION_EXPORT.test.ts # test 6 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/DS9_REGION_EXPORT.test.ts # test 7 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/DS9_REGION_IMPORT_EXCEPTION.test.ts # test 8 of 9"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/DS9_REGION_IMPORT_EXPORT.test.ts # test 9 of 9"
                sh "pgrep carta_backend"
            }
        }
    }
}

def cube_histogram() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM_HDF5.test.ts # test 1 of 1"
            }
        }
    }
}

def spatial_profiler() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE.test.ts # test 1 of 2"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE_NaN.test.ts # test 2 of 2"
                sh "pgrep carta_backend"
            }
        }
    }
}

def raster_tiles() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/CHECK_RASTER_TILE_DATA.test.ts # test 1 of 2"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/TILE_DATA_REQUEST.test.ts # test 2 of 2"
                sh "pgrep carta_backend"
            }
        }
    }
}

def catalog() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/CATALOG_GENERAL.test.ts # test 1 of 1"
            }
        }
    }
}

def moment() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/MOMENTS_GENERATOR_CASA.test.ts # test 1 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/MOMENTS_GENERATOR_EXCEPTION.test.ts # test 2 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/MOMENTS_GENERATOR_FITS.test.ts # test 3 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/MOMENTS_GENERATOR_HDF5.test.ts # test 4 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/MOMENTS_GENERATOR_SAVE.test.ts # test 5 of 6"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/MOMENTS_GENERATOR_CANCEL.test.ts # test 6 of 6"
                sh "pgrep carta_backend"
            }
        }
    }
}

def resume() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/RESUME_CATALOG.test.ts # test 1 of 4"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/RESUME_CONTOUR.test.ts # test 2 of 4"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/RESUME_IMAGE.test.ts # test 3 of 4"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/RESUME_REGION.test.ts # test 4 of 4"
                sh "pgrep carta_backend"
            }
        }
    }
}

def match() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                     ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/MATCH_SPATIAL.test.ts # test 1 of 2"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/MATCH_STATS.test.ts # test 2 of 2"
                sh "pgrep carta_backend"
            }
        }
    }
}

def close_file() {
    script {
        dir ('carta-backend-ICD-test') {
            sh "npm install && ./protobuf/build_proto.sh"
            ret = false
            retry(3) {
                if (ret) {
                    sleep(time:30,unit:"SECONDS")
                    sh "cat /root/.carta/log/carta.log"
                    echo "Trying again"
                } else {
                    ret = true
                }
                sh "pgrep carta_backend"
                sh "CI=true npm test src/test/CLOSE_FILE_SINGLE.test.ts # test 1 of 5"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CLOSE_FILE_ANIMATION.test.ts # test 2 of 5"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CLOSE_FILE_ERROR.test.ts # test 3 of 5"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CLOSE_FILE_SPECTRAL_PROFILE.test.ts # test 4 of 5"
                sh "sleep 3 && pgrep carta_backend"
                sh "CI=true npm test src/test/CLOSE_FILE_TILE.test.ts # test 5 of 5"
                sh "pgrep carta_backend"
            }
        }
    }
}
