/*
 * ShaderConductor
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License.
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
 * to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <ShaderConductor/ShaderConductor.hpp>

#include <dxc/Support/Global.h>
#include <dxc/Support/Unicode.h>
#include <dxc/Support/WinAdapter.h>
#include <dxc/Support/WinIncludes.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <fstream>
#include <memory>

#include <dxc/dxcapi.h>
/* UE Change Begin: Add functionality to rewrite HLSL to remove unused code and globals */
#include <dxc/dxctools.h>
/* UE Change End: Add functionality to rewrite HLSL to remove unused code and globals */
#include <llvm/Support/ErrorHandling.h>

#include <spirv-tools/libspirv.h>
#include <spirv.hpp>
#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>

#define SC_UNUSED(x) (void)(x);

using namespace ShaderConductor;

namespace
{
    bool dllDetaching = false;

    class Dxcompiler
    {
    public:
        ~Dxcompiler()
        {
            this->Destroy();
        }

        static Dxcompiler& Instance()
        {
            static Dxcompiler instance;
            return instance;
        }

        IDxcLibrary* Library() const
        {
            return m_library;
        }

        IDxcCompiler* Compiler() const
        {
            return m_compiler;
        }
		
		/* UE Change Begin: Add functionality to rewrite HLSL to remove unused code and globals */
		IDxcRewriter* Rewriter() const
		{
			return m_rewriter;
		}
		/* UE Change End: Add functionality to rewrite HLSL to remove unused code and globals */

        void Destroy()
        {
            if (m_dxcompilerDll)
            {
                m_compiler = nullptr;
                m_library = nullptr;

                m_createInstanceFunc = nullptr;

#ifdef _WIN32
                ::FreeLibrary(m_dxcompilerDll);
#else
                ::dlclose(m_dxcompilerDll);
#endif

                m_dxcompilerDll = nullptr;
            }
        }

        void Terminate()
        {
            if (m_dxcompilerDll)
            {
                m_compiler.Detach();
                m_library.Detach();
				/* UE Change Begin: Add functionality to rewrite HLSL to remove unused code and globals */
				m_rewriter.Detach();
				/* UE Change End: Add functionality to rewrite HLSL to remove unused code and globals */

                m_createInstanceFunc = nullptr;

                m_dxcompilerDll = nullptr;
            }
        }

    private:
        Dxcompiler()
        {
            if (dllDetaching)
            {
                return;
            }

#ifdef _WIN32
            const char* dllName = "dxcompiler_sc.dll";
#elif __APPLE__
            const char* dllName = "libdxcompiler.3.7.dylib";
#else
            const char* dllName = "libdxcompiler.so";
#endif
            const char* functionName = "DxcCreateInstance";

#ifdef _WIN32
            m_dxcompilerDll = ::LoadLibraryA(dllName);
#else
            m_dxcompilerDll = ::dlopen(dllName, RTLD_LAZY);
#endif

            if (m_dxcompilerDll != nullptr)
            {
#ifdef _WIN32
                m_createInstanceFunc = (DxcCreateInstanceProc)::GetProcAddress(m_dxcompilerDll, functionName);
#else
                m_createInstanceFunc = (DxcCreateInstanceProc)::dlsym(m_dxcompilerDll, functionName);
#endif

                if (m_createInstanceFunc != nullptr)
                {
                    IFT(m_createInstanceFunc(CLSID_DxcLibrary, __uuidof(IDxcLibrary), reinterpret_cast<void**>(&m_library)));
                    IFT(m_createInstanceFunc(CLSID_DxcCompiler, __uuidof(IDxcCompiler), reinterpret_cast<void**>(&m_compiler)));
					/* UE Change Begin: Add functionality to rewrite HLSL to remove unused code and globals */
					IFT(m_createInstanceFunc(CLSID_DxcRewriter, __uuidof(IDxcRewriter), reinterpret_cast<void**>(&m_rewriter)));
					/* UE Change End: Add functionality to rewrite HLSL to remove unused code and globals */
                }
                else
                {
                    this->Destroy();

                    throw std::runtime_error(std::string("COULDN'T get ") + functionName + " from dxcompiler.");
                }
            }
            else
            {
                throw std::runtime_error("COULDN'T load dxcompiler.");
            }
        }

    private:
        HMODULE m_dxcompilerDll = nullptr;
        DxcCreateInstanceProc m_createInstanceFunc = nullptr;

        CComPtr<IDxcLibrary> m_library;
        CComPtr<IDxcCompiler> m_compiler;
		/* UE Change Begin: Add functionality to rewrite HLSL to remove unused code and globals */
		CComPtr<IDxcRewriter> m_rewriter;
		/* UE Change End: Add functionality to rewrite HLSL to remove unused code and globals */
    };

    class ScIncludeHandler : public IDxcIncludeHandler
    {
    public:
        explicit ScIncludeHandler(std::function<Blob*(const char* includeName)> loadCallback) : m_loadCallback(std::move(loadCallback))
        {
        }

        HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR fileName, IDxcBlob** includeSource) override
        {
            if ((fileName[0] == L'.') && (fileName[1] == L'/'))
            {
                fileName += 2;
            }

            std::string utf8FileName;
            if (!Unicode::UTF16ToUTF8String(fileName, &utf8FileName))
            {
                return E_FAIL;
            }

            auto blobDeleter = [](Blob* blob) { DestroyBlob(blob); };

            std::unique_ptr<Blob, decltype(blobDeleter)> source(nullptr, blobDeleter);
            try
            {
                source.reset(m_loadCallback(utf8FileName.c_str()));
            }
            catch (...)
            {
                return E_FAIL;
            }

            *includeSource = nullptr;
            return Dxcompiler::Instance().Library()->CreateBlobWithEncodingOnHeapCopy(
                source->Data(), source->Size(), CP_UTF8, reinterpret_cast<IDxcBlobEncoding**>(includeSource));
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            ++m_ref;
            return m_ref;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            --m_ref;
            ULONG result = m_ref;
            if (result == 0)
            {
                delete this;
            }
            return result;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
        {
            if (IsEqualIID(iid, __uuidof(IDxcIncludeHandler)))
            {
                *object = dynamic_cast<IDxcIncludeHandler*>(this);
                this->AddRef();
                return S_OK;
            }
            else if (IsEqualIID(iid, __uuidof(IUnknown)))
            {
                *object = dynamic_cast<IUnknown*>(this);
                this->AddRef();
                return S_OK;
            }
            else
            {
                return E_NOINTERFACE;
            }
        }

    private:
        std::function<Blob*(const char* includeName)> m_loadCallback;

        std::atomic<ULONG> m_ref = 0;
    };

    Blob* DefaultLoadCallback(const char* includeName)
    {
        std::vector<char> ret;
        std::ifstream includeFile(includeName, std::ios_base::in);
        if (includeFile)
        {
            includeFile.seekg(0, std::ios::end);
            ret.resize(includeFile.tellg());
            includeFile.seekg(0, std::ios::beg);
            includeFile.read(ret.data(), ret.size());
            while (!ret.empty() && (ret.back() == '\0'))
            {
                ret.pop_back();
            }
        }
        else
        {
            throw std::runtime_error(std::string("COULDN'T load included file ") + includeName + ".");
        }
        return CreateBlob(ret.data(), static_cast<uint32_t>(ret.size()));
    }

    class ScBlob : public Blob
    {
    public:
        ScBlob(const void* data, uint32_t size)
            : data_(reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + size)
        {
        }

        const void* Data() const override
        {
            return data_.data();
        }

        uint32_t Size() const override
        {
            return static_cast<uint32_t>(data_.size());
        }

    private:
        std::vector<uint8_t> data_;
    };

    void AppendError(Compiler::ResultDesc& result, const std::string& msg)
    {
        std::string errorMSg;
        if (result.errorWarningMsg != nullptr)
        {
            errorMSg.assign(reinterpret_cast<const char*>(result.errorWarningMsg->Data()), result.errorWarningMsg->Size());
        }
        if (!errorMSg.empty())
        {
            errorMSg += "\n";
        }
        errorMSg += msg;
        DestroyBlob(result.errorWarningMsg);
        result.errorWarningMsg = CreateBlob(errorMSg.data(), static_cast<uint32_t>(errorMSg.size()));
        result.hasError = true;
    }
    
    /* UE Change Begin: Add functionality to rewrite HLSL to remove unused code and globals */
    Compiler::ResultDesc RewriteHlsl(const Compiler::SourceDesc& source, const Compiler::Options& options)
    {
        CComPtr<IDxcBlobEncoding> sourceBlob;
		IFT(Dxcompiler::Instance().Library()->CreateBlobWithEncodingOnHeapCopy(source.source, static_cast<UINT32>(strlen(source.source)),
																			   CP_UTF8, &sourceBlob));
        IFTARG(sourceBlob->GetBufferSize() >= 4);
        
        std::wstring shaderNameUtf16;
        Unicode::UTF8ToUTF16String(source.fileName, &shaderNameUtf16);
        
        std::wstring entryPointUtf16;
        Unicode::UTF8ToUTF16String(source.entryPoint, &entryPointUtf16);
        
        std::vector<DxcDefine> dxcDefines;
        std::vector<std::wstring> dxcDefineStrings;
        // Need to reserve capacity so that small-string optimization does not
        // invalidate the pointers to internal string data while resizing.
        dxcDefineStrings.reserve(source.numDefines * 2);
        for (size_t i = 0; i < source.numDefines; ++i)
        {
            const auto& define = source.defines[i];
            
            std::wstring nameUtf16Str;
            Unicode::UTF8ToUTF16String(define.name, &nameUtf16Str);
            dxcDefineStrings.emplace_back(std::move(nameUtf16Str));
            const wchar_t* nameUtf16 = dxcDefineStrings.back().c_str();
            
            const wchar_t* valueUtf16;
            if (define.value != nullptr)
            {
                std::wstring valueUtf16Str;
                Unicode::UTF8ToUTF16String(define.value, &valueUtf16Str);
                dxcDefineStrings.emplace_back(std::move(valueUtf16Str));
                valueUtf16 = dxcDefineStrings.back().c_str();
            }
            else
            {
                valueUtf16 = nullptr;
            }
            
            dxcDefines.push_back({ nameUtf16, valueUtf16 });
        }
        
        CComPtr<IDxcOperationResult> rewriteResult;
        CComPtr<IDxcIncludeHandler> includeHandler = new ScIncludeHandler(std::move(source.loadIncludeCallback));
        IFT(Dxcompiler::Instance().Rewriter()->RewriteUnchangedWithInclude(sourceBlob,
                                                                           shaderNameUtf16.c_str(),
                                                                           dxcDefines.data(),
                                                                           static_cast<UINT32>(dxcDefines.size()),
                                                                           includeHandler,
                                                                           0,
                                                                           &rewriteResult));
        
        HRESULT statusRewrite;
        IFT(rewriteResult->GetStatus(&statusRewrite));
		
		Compiler::ResultDesc ret;
		ret.isText = true;
		ret.hasError = true;

		if (SUCCEEDED(statusRewrite))
        {
			CComPtr<IDxcBlobEncoding> rewritten;
			
			CComPtr<IDxcBlobEncoding> temp;
            IFT(rewriteResult->GetResult((IDxcBlob**)&temp));
            
            if (options.removeUnusedGlobals)
            {
				CComPtr<IDxcOperationResult> removeUnusedGlobalsResult;
                IFT(Dxcompiler::Instance().Rewriter()->RemoveUnusedGlobals(temp, entryPointUtf16.c_str(), dxcDefines.data(),
                                                                           static_cast<UINT32>(dxcDefines.size()), &removeUnusedGlobalsResult));
				IFT(removeUnusedGlobalsResult->GetStatus(&statusRewrite));
                
                if (SUCCEEDED(statusRewrite))
                {
                    IFT(removeUnusedGlobalsResult->GetResult((IDxcBlob**)&rewritten));
					ret.hasError = false;
					ret.target = CreateBlob(rewritten->GetBufferPointer(), static_cast<uint32_t>(rewritten->GetBufferSize()));
                }
            }
			else
			{
				IFT(rewriteResult->GetResult((IDxcBlob**)&rewritten));
				ret.hasError = false;
				ret.target = CreateBlob(rewritten->GetBufferPointer(), static_cast<uint32_t>(rewritten->GetBufferSize()));
			}
        }
		else
		{
			ret.target = CreateBlob(sourceBlob->GetBufferPointer(), static_cast<uint32_t>(sourceBlob->GetBufferSize()));
		}
        
        return ret;
    }
    /* UE Change End: Add functionality to rewrite HLSL to remove unused code and globals */

    Compiler::ResultDesc CompileToBinary(const Compiler::SourceDesc& source, const Compiler::Options& options,
                                         ShadingLanguage targetLanguage)
    {
        assert((targetLanguage == ShadingLanguage::Dxil) || (targetLanguage == ShadingLanguage::SpirV));

        std::wstring shaderProfile;
        switch (source.stage)
        {
        case ShaderStage::VertexShader:
            shaderProfile = L"vs";
            break;

        case ShaderStage::PixelShader:
            shaderProfile = L"ps";
            break;

        case ShaderStage::GeometryShader:
            shaderProfile = L"gs";
            break;

        case ShaderStage::HullShader:
            shaderProfile = L"hs";
            break;

        case ShaderStage::DomainShader:
            shaderProfile = L"ds";
            break;

        case ShaderStage::ComputeShader:
            shaderProfile = L"cs";
            break;

        default:
            llvm_unreachable("Invalid shader stage.");
        }
        shaderProfile.push_back(L'_');
        shaderProfile.push_back(L'0' + options.shaderModel.major_ver);
        shaderProfile.push_back(L'_');
        shaderProfile.push_back(L'0' + options.shaderModel.minor_ver);

        std::vector<DxcDefine> dxcDefines;
        std::vector<std::wstring> dxcDefineStrings;
        // Need to reserve capacity so that small-string optimization does not
        // invalidate the pointers to internal string data while resizing.
        dxcDefineStrings.reserve(source.numDefines * 2);
        for (size_t i = 0; i < source.numDefines; ++i)
        {
            const auto& define = source.defines[i];

            std::wstring nameUtf16Str;
            Unicode::UTF8ToUTF16String(define.name, &nameUtf16Str);
            dxcDefineStrings.emplace_back(std::move(nameUtf16Str));
            const wchar_t* nameUtf16 = dxcDefineStrings.back().c_str();

            const wchar_t* valueUtf16;
            if (define.value != nullptr)
            {
                std::wstring valueUtf16Str;
                Unicode::UTF8ToUTF16String(define.value, &valueUtf16Str);
                dxcDefineStrings.emplace_back(std::move(valueUtf16Str));
                valueUtf16 = dxcDefineStrings.back().c_str();
            }
            else
            {
                valueUtf16 = nullptr;
            }

            dxcDefines.push_back({ nameUtf16, valueUtf16 });
        }

        CComPtr<IDxcBlobEncoding> sourceBlob;
        IFT(Dxcompiler::Instance().Library()->CreateBlobWithEncodingOnHeapCopy(source.source, static_cast<UINT32>(strlen(source.source)),
                                                                               CP_UTF8, &sourceBlob));
        IFTARG(sourceBlob->GetBufferSize() >= 4);

        std::wstring shaderNameUtf16;
        Unicode::UTF8ToUTF16String(source.fileName, &shaderNameUtf16);

        std::wstring entryPointUtf16;
        Unicode::UTF8ToUTF16String(source.entryPoint, &entryPointUtf16);

        std::vector<std::wstring> dxcArgStrings;

        // HLSL matrices are translated into SPIR-V OpTypeMatrixs in a transposed manner,
        // See also https://antiagainst.github.io/post/hlsl-for-vulkan-matrices/
        if (options.packMatricesInRowMajor)
        {
            dxcArgStrings.push_back(L"-Zpc");
        }
        else
        {
            dxcArgStrings.push_back(L"-Zpr");
        }

        if (options.enable16bitTypes)
        {
            if (options.shaderModel >= Compiler::ShaderModel{ 6, 2 })
            {
                dxcArgStrings.push_back(L"-enable-16bit-types");
            }
            else
            {
                throw std::runtime_error("16-bit types requires shader model 6.2 or up.");
            }
        }

        if (options.enableDebugInfo)
        {
            dxcArgStrings.push_back(L"-Zi");
        }

        if (options.disableOptimizations)
        {
            dxcArgStrings.push_back(L"-Od");
        }
        else
        {
            if (options.optimizationLevel < 4)
            {
                dxcArgStrings.push_back(std::wstring(L"-O") + static_cast<wchar_t>(L'0' + options.optimizationLevel));
            }
            else
            {
                llvm_unreachable("Invalid optimization level.");
            }
        }

        switch (targetLanguage)
        {
        case ShadingLanguage::Dxil:
            break;

        case ShadingLanguage::SpirV:
        case ShadingLanguage::Hlsl:
        case ShadingLanguage::Glsl:
        case ShadingLanguage::Essl:
        case ShadingLanguage::Msl:
            dxcArgStrings.push_back(L"-spirv");
			/* UE Change Begin: Specify SPIRV reflection so that we retain semantic strings! */
			dxcArgStrings.push_back(L"-fspv-reflect");
			/* UE Change End: Specify SPIRV reflection so that we retain semantic strings! */
			/* UE Change Begin: Emit SPIRV debug info when asked to */
			if (options.enableDebugInfo)
				dxcArgStrings.push_back(L"-fspv-debug=line");
			/* UE Change End: Emit SPIRV debug info when asked to */
            break;

        default:
            llvm_unreachable("Invalid shading language.");
        }

        std::vector<const wchar_t*> dxcArgs;
        dxcArgs.reserve(dxcArgStrings.size());
        for (const auto& arg : dxcArgStrings)
        {
            dxcArgs.push_back(arg.c_str());
        }

        CComPtr<IDxcIncludeHandler> includeHandler = new ScIncludeHandler(std::move(source.loadIncludeCallback));
        CComPtr<IDxcOperationResult> compileResult;
        IFT(Dxcompiler::Instance().Compiler()->Compile(sourceBlob, shaderNameUtf16.c_str(), entryPointUtf16.c_str(), shaderProfile.c_str(),
                                                       dxcArgs.data(), static_cast<UINT32>(dxcArgs.size()), dxcDefines.data(),
                                                       static_cast<UINT32>(dxcDefines.size()), includeHandler, &compileResult));

        HRESULT status;
        IFT(compileResult->GetStatus(&status));

        Compiler::ResultDesc ret;

        ret.target = nullptr;
        ret.isText = false;
        ret.errorWarningMsg = nullptr;

        CComPtr<IDxcBlobEncoding> errors;
        IFT(compileResult->GetErrorBuffer(&errors));
        if (errors != nullptr)
        {
            if (errors->GetBufferSize() > 0)
            {
                ret.errorWarningMsg = CreateBlob(errors->GetBufferPointer(), static_cast<uint32_t>(errors->GetBufferSize()));
            }
            errors = nullptr;
        }

        ret.hasError = true;
        if (SUCCEEDED(status))
        {
            CComPtr<IDxcBlob> program;
            IFT(compileResult->GetResult(&program));
            compileResult = nullptr;
            if (program != nullptr)
            {
                ret.target = CreateBlob(program->GetBufferPointer(), static_cast<uint32_t>(program->GetBufferSize()));
                ret.hasError = false;
            }
        }

        return ret;
    }
/* UE Change Begin: Two stage compilation is preferable for UE4 as it avoids polluting SC with SPIRV->MSL complexities. */
} // namespace

namespace ShaderConductor
{
    Compiler::ResultDesc Compiler::ConvertBinary(const Compiler::ResultDesc& binaryResult, const Compiler::SourceDesc& source,
                                       const Compiler::TargetDesc& target)
/* UE Change End: Two stage compilation is preferable for UE4 as it avoids polluting SC with SPIRV->MSL complexities. */
    {
        assert((target.language != ShadingLanguage::Dxil) && (target.language != ShadingLanguage::SpirV));
        assert((binaryResult.target->Size() & (sizeof(uint32_t) - 1)) == 0);

        Compiler::ResultDesc ret;

        ret.target = nullptr;
        ret.errorWarningMsg = binaryResult.errorWarningMsg;
        ret.isText = true;

        uint32_t intVersion = 0;
        if (target.version != nullptr)
        {
            intVersion = std::stoi(target.version);
        }

        const uint32_t* spirvIr = reinterpret_cast<const uint32_t*>(binaryResult.target->Data());
        const size_t spirvSize = binaryResult.target->Size() / sizeof(uint32_t);

        std::unique_ptr<spirv_cross::CompilerGLSL> compiler;
        bool combinedImageSamplers = false;
        bool buildDummySampler = false;

        switch (target.language)
        {
        case ShadingLanguage::Hlsl:
            if ((source.stage == ShaderStage::GeometryShader) || (source.stage == ShaderStage::HullShader) ||
                (source.stage == ShaderStage::DomainShader))
            {
                // Check https://github.com/KhronosGroup/SPIRV-Cross/issues/121 for details
                AppendError(ret, "GS, HS, and DS has not been supported yet.");
                return ret;
            }
            if ((source.stage == ShaderStage::GeometryShader) && (intVersion < 40))
            {
                AppendError(ret, "HLSL shader model earlier than 4.0 doesn't have GS or CS.");
                return ret;
            }
            if ((source.stage == ShaderStage::ComputeShader) && (intVersion < 50))
            {
                AppendError(ret, "CS in HLSL shader model earlier than 5.0 is not supported.");
                return ret;
            }
            if (((source.stage == ShaderStage::HullShader) || (source.stage == ShaderStage::DomainShader)) && (intVersion < 50))
            {
                AppendError(ret, "HLSL shader model earlier than 5.0 doesn't have HS or DS.");
                return ret;
            }
            compiler = std::make_unique<spirv_cross::CompilerHLSL>(spirvIr, spirvSize);
            break;

        case ShadingLanguage::Glsl:
        case ShadingLanguage::Essl:
            compiler = std::make_unique<spirv_cross::CompilerGLSL>(spirvIr, spirvSize);
            combinedImageSamplers = true;
            buildDummySampler = true;
            break;

        case ShadingLanguage::Msl:
            if (source.stage == ShaderStage::GeometryShader)
            {
                AppendError(ret, "MSL doesn't have GS.");
                return ret;
            }
            compiler = std::make_unique<spirv_cross::CompilerMSL>(spirvIr, spirvSize);
            break;

        default:
            llvm_unreachable("Invalid target language.");
        }

        spv::ExecutionModel model;
        switch (source.stage)
        {
        case ShaderStage::VertexShader:
            model = spv::ExecutionModelVertex;
            break;

        case ShaderStage::HullShader:
            model = spv::ExecutionModelTessellationControl;
            break;

        case ShaderStage::DomainShader:
            model = spv::ExecutionModelTessellationEvaluation;
            break;

        case ShaderStage::GeometryShader:
            model = spv::ExecutionModelGeometry;
            break;

        case ShaderStage::PixelShader:
            model = spv::ExecutionModelFragment;
            break;

        case ShaderStage::ComputeShader:
            model = spv::ExecutionModelGLCompute;
            break;

        default:
            llvm_unreachable("Invalid shader stage.");
        }
        compiler->set_entry_point(source.entryPoint, model);

        spirv_cross::CompilerGLSL::Options opts = compiler->get_common_options();
        if (target.version != nullptr)
        {
            opts.version = intVersion;
        }
        opts.es = (target.language == ShadingLanguage::Essl);
        opts.force_temporary = false;
        opts.separate_shader_objects = true;
        opts.flatten_multidimensional_arrays = false;
        opts.enable_420pack_extension =
            (target.language == ShadingLanguage::Glsl) && ((target.version == nullptr) || (opts.version >= 420));
        opts.vulkan_semantics = false;
        opts.vertex.fixup_clipspace = false;
        opts.vertex.flip_vert_y = false;
        opts.vertex.support_nonzero_base_instance = true;
        compiler->set_common_options(opts);

        if (target.language == ShadingLanguage::Hlsl)
        {
            auto* hlslCompiler = static_cast<spirv_cross::CompilerHLSL*>(compiler.get());
            auto hlslOpts = hlslCompiler->get_hlsl_options();
            if (target.version != nullptr)
            {
                if (opts.version < 30)
                {
                    AppendError(ret, "HLSL shader model earlier than 3.0 is not supported.");
                    return ret;
                }
                hlslOpts.shader_model = opts.version;
            }

            if (hlslOpts.shader_model <= 30)
            {
                combinedImageSamplers = true;
                buildDummySampler = true;
            }

            hlslCompiler->set_hlsl_options(hlslOpts);
        }
        else if (target.language == ShadingLanguage::Msl)
        {
            auto* mslCompiler = static_cast<spirv_cross::CompilerMSL*>(compiler.get());
            auto mslOpts = mslCompiler->get_msl_options();
            if (target.version != nullptr)
            {
                mslOpts.msl_version = opts.version;
            }
			/* UE Change Begin: Support reflection & overriding Metal options & resource bindings to generate correct code */
			if (target.platform != nullptr)
			{
				if (!strcmp(target.platform,"macOS"))
				{
					mslOpts.platform = spirv_cross::CompilerMSL::Options::macOS;
				}
				else
				{
					mslOpts.platform = spirv_cross::CompilerMSL::Options::iOS;
				}
			}
			mslOpts.swizzle_texture_samples = false;
            for (unsigned i = 0; i < target.numOptions; i++)
			{
                auto& Define = target.options[i];
				if (!strcmp(Define.name, "ios_support_base_vertex_instance"))
				{
					mslOpts.ios_support_base_vertex_instance = (std::stoi(Define.value) != 0);
				}
				if (!strcmp(Define.name, "swizzle_texture_samples"))
				{
					mslOpts.swizzle_texture_samples = (std::stoi(Define.value) != 0);
				}
				if (!strcmp(Define.name, "texel_buffer_texture_width"))
				{
					mslOpts.texel_buffer_texture_width = (uint32_t)std::stoi(Define.value);
				}
                /* UE Change Begin: Use Metal's native texture-buffer type for HLSL buffers. */
				if (!strcmp(Define.name, "texture_buffer_native"))
				{
					mslOpts.texture_buffer_native = (std::stoi(Define.value) != 0);
                }
                /* UE Change End: Use Metal's native texture-buffer type for HLSL buffers. */
                /* UE Change Begin: Use Metal's native frame-buffer fetch API for subpass inputs. */
				if (!strcmp(Define.name, "ios_use_framebuffer_fetch_subpasses"))
				{
					mslOpts.ios_use_framebuffer_fetch_subpasses = (std::stoi(Define.value) != 0);
				}
                /* UE Change End: Use Metal's native frame-buffer fetch API for subpass inputs. */
				/* UE Change Begin: Storage buffer robustness - clamps access to SSBOs to the size of the buffer */
				if (!strcmp(Define.name, "enforce_storge_buffer_bounds"))
				{
					mslOpts.enforce_storge_buffer_bounds = (std::stoi(Define.value) != 0);
				}
				if (!strcmp(Define.name, "buffer_size_buffer_index"))
				{
					mslOpts.buffer_size_buffer_index = (uint32_t)std::stoi(Define.value);
				}
				/* UE Change End: Storage buffer robustness - clamps access to SSBOs to the size of the buffer */
				/* UE Change Begin: Capture shader output to a buffer - used for vertex streaming to emulate GS & Tess */
				if (!strcmp(Define.name, "capture_output_to_buffer"))
				{
					mslOpts.capture_output_to_buffer = (std::stoi(Define.value) != 0);
				}
				if (!strcmp(Define.name, "shader_output_buffer_index"))
				{
					mslOpts.shader_output_buffer_index = (uint32_t)std::stoi(Define.value);
				}
				/* UE Change End: Capture shader output to a buffer - used for vertex streaming to emulate GS & Tess */
				/* UE Change Begin: Allow the caller to specify the various auxiliary Metal buffer indices */
				if (!strcmp(Define.name, "indirect_params_buffer_index"))
				{
					mslOpts.indirect_params_buffer_index = (uint32_t)std::stoi(Define.value);
				}
				if (!strcmp(Define.name, "shader_patch_output_buffer_index"))
				{
					mslOpts.shader_patch_output_buffer_index = (uint32_t)std::stoi(Define.value);
				}
				if (!strcmp(Define.name, "shader_tess_factor_buffer_index"))
				{
					mslOpts.shader_tess_factor_buffer_index = (uint32_t)std::stoi(Define.value);
				}
				if (!strcmp(Define.name, "shader_input_wg_index"))
				{
					mslOpts.shader_input_wg_index = (uint32_t)std::stoi(Define.value);
				}
				/* UE Change End: Allow the caller to specify the various auxiliary Metal buffer indices */
			}
			
			mslCompiler->set_msl_options(mslOpts);
			/* UE Change End: Support reflection & overriding Metal options & resource bindings to generate correct code */
        }

        if (buildDummySampler)
        {
            const uint32_t sampler = compiler->build_dummy_sampler_for_combined_images();
            if (sampler != 0)
            {
                compiler->set_decoration(sampler, spv::DecorationDescriptorSet, 0);
                compiler->set_decoration(sampler, spv::DecorationBinding, 0);
            }
        }

        if (combinedImageSamplers)
        {
            compiler->build_combined_image_samplers();

            for (auto& remap : compiler->get_combined_image_samplers())
            {
                compiler->set_name(remap.combined_id,
                                   "SPIRV_Cross_Combined" + compiler->get_name(remap.image_id) + compiler->get_name(remap.sampler_id));
            }
        }

        if (target.language == ShadingLanguage::Hlsl)
        {
            auto* hlslCompiler = static_cast<spirv_cross::CompilerHLSL*>(compiler.get());
            const uint32_t newBuiltin = hlslCompiler->remap_num_workgroups_builtin();
            if (newBuiltin)
            {
                compiler->set_decoration(newBuiltin, spv::DecorationDescriptorSet, 0);
                compiler->set_decoration(newBuiltin, spv::DecorationBinding, 0);
            }
        }

        try
        {
            const std::string targetStr = compiler->compile();
            ret.target = CreateBlob(targetStr.data(), static_cast<uint32_t>(targetStr.size()));
            ret.hasError = false;
        }
        catch (spirv_cross::CompilerError& error)
        {
            const char* errorMsg = error.what();
            DestroyBlob(ret.errorWarningMsg);
            ret.errorWarningMsg = CreateBlob(errorMsg, static_cast<uint32_t>(strlen(errorMsg)));
            ret.hasError = true;
        }

        return ret;
    }
    
/* UE Change Begin: Two stage compilation is preferable for UE4 as it avoids polluting SC with SPIRV->MSL complexities. */
/* UE Change End: Two stage compilation is preferable for UE4 as it avoids polluting SC with SPIRV->MSL complexities. */
    
    Blob::~Blob() = default;

    Blob* CreateBlob(const void* data, uint32_t size)
    {
        return new ScBlob(data, size);
    }

    void DestroyBlob(Blob* blob)
    {
        delete blob;
    }

    Compiler::ResultDesc Compiler::Compile(const SourceDesc& source, const Options& options, const TargetDesc& target)
    {
        ResultDesc result;
        Compiler::Compile(source, options, &target, 1, &result);
        return result;
    }

    void Compiler::Compile(const SourceDesc& source, const Options& options, const TargetDesc* targets, uint32_t numTargets,
                           ResultDesc* results)
    {
        SourceDesc sourceOverride = source;
        if (!sourceOverride.entryPoint || (strlen(sourceOverride.entryPoint) == 0))
        {
            sourceOverride.entryPoint = "main";
        }
        if (!sourceOverride.loadIncludeCallback)
        {
            sourceOverride.loadIncludeCallback = DefaultLoadCallback;
        }

        bool hasDxil = false;
        bool hasSpirV = false;
        for (uint32_t i = 0; i < numTargets; ++i)
        {
            if (targets[i].language == ShadingLanguage::Dxil)
            {
                hasDxil = true;
            }
            else
            {
                hasSpirV = true;
            }
        }

        ResultDesc dxilBinaryResult{};
        if (hasDxil)
        {
            dxilBinaryResult = CompileToBinary(sourceOverride, options, ShadingLanguage::Dxil);
        }

        ResultDesc spirvBinaryResult{};
        if (hasSpirV)
        {
            spirvBinaryResult = CompileToBinary(sourceOverride, options, ShadingLanguage::SpirV);
        }

        for (uint32_t i = 0; i < numTargets; ++i)
        {
            ResultDesc binaryResult = targets[i].language == ShadingLanguage::Dxil ? dxilBinaryResult : spirvBinaryResult;
            if (binaryResult.target)
            {
                binaryResult.target = CreateBlob(binaryResult.target->Data(), binaryResult.target->Size());
            }
            if (binaryResult.errorWarningMsg)
            {
                binaryResult.errorWarningMsg = CreateBlob(binaryResult.errorWarningMsg->Data(), binaryResult.errorWarningMsg->Size());
            }
            if (!binaryResult.hasError)
            {
                switch (targets[i].language)
                {
                case ShadingLanguage::Dxil:
                case ShadingLanguage::SpirV:
                    results[i] = binaryResult;
                    break;

                case ShadingLanguage::Hlsl:
                case ShadingLanguage::Glsl:
                case ShadingLanguage::Essl:
                case ShadingLanguage::Msl:
                    results[i] = ConvertBinary(binaryResult, sourceOverride, targets[i]);
                    break;

                default:
                    llvm_unreachable("Invalid shading language.");
                    break;
                }
            }
            else
            {
                results[i] = binaryResult;
            }
        }

        if (hasDxil)
        {
            DestroyBlob(dxilBinaryResult.target);
            DestroyBlob(dxilBinaryResult.errorWarningMsg);
        }
        if (hasSpirV)
        {
            DestroyBlob(spirvBinaryResult.target);
            DestroyBlob(spirvBinaryResult.errorWarningMsg);
        }
    }

    Compiler::ResultDesc Compiler::Disassemble(const DisassembleDesc& source)
    {
        assert((source.language == ShadingLanguage::SpirV) || (source.language == ShadingLanguage::Dxil));

        Compiler::ResultDesc ret;

        ret.target = nullptr;
        ret.isText = true;
        ret.errorWarningMsg = nullptr;

        if (source.language == ShadingLanguage::SpirV)
        {
            const uint32_t* spirvIr = reinterpret_cast<const uint32_t*>(source.binary);
            const size_t spirvSize = source.binarySize / sizeof(uint32_t);

            spv_context context = spvContextCreate(SPV_ENV_UNIVERSAL_1_3);
            uint32_t options = SPV_BINARY_TO_TEXT_OPTION_NONE | SPV_BINARY_TO_TEXT_OPTION_INDENT | SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES;
            spv_text text = nullptr;
            spv_diagnostic diagnostic = nullptr;

            spv_result_t error = spvBinaryToText(context, spirvIr, spirvSize, options, &text, &diagnostic);
            spvContextDestroy(context);

            if (error)
            {
                ret.errorWarningMsg = CreateBlob(diagnostic->error, static_cast<uint32_t>(strlen(diagnostic->error)));
                ret.hasError = true;
                spvDiagnosticDestroy(diagnostic);
            }
            else
            {
                const std::string disassemble = text->str;
                ret.target = CreateBlob(disassemble.data(), static_cast<uint32_t>(disassemble.size()));
                ret.hasError = false;
            }

            spvTextDestroy(text);
        }
        else
        {
            CComPtr<IDxcBlobEncoding> blob;
            CComPtr<IDxcBlobEncoding> disassembly;
            IFT(Dxcompiler::Instance().Library()->CreateBlobWithEncodingOnHeapCopy(source.binary, source.binarySize, CP_UTF8, &blob));
            IFT(Dxcompiler::Instance().Compiler()->Disassemble(blob, &disassembly));

            if (disassembly != nullptr)
            {
                ret.target = CreateBlob(disassembly->GetBufferPointer(), static_cast<uint32_t>(disassembly->GetBufferSize()));
                ret.hasError = false;
            }
            else
            {
                ret.hasError = true;
            }
        }

        return ret;
    }
	
	/* UE Change Begin: Add functionality to rewrite HLSL to remove unused code and globals */
	Compiler::ResultDesc Compiler::Rewrite(SourceDesc source, const Compiler::Options& options)
	{
		if (source.entryPoint == nullptr)
		{
			source.entryPoint = "main";
		}
		if (!source.loadIncludeCallback)
		{
			source.loadIncludeCallback = DefaultLoadCallback;
		}
		
		auto ret = RewriteHlsl(source, options);
		return ret;
	}
	/* UE Change End: Add functionality to rewrite HLSL to remove unused code and globals */
} // namespace ShaderConductor

#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    SC_UNUSED(instance);

    BOOL result = TRUE;
    if (reason == DLL_PROCESS_DETACH)
    {
        dllDetaching = true;

        if (reserved == 0)
        {
            // FreeLibrary has been called or the DLL load failed
            Dxcompiler::Instance().Destroy();
        }
        else
        {
            // Process termination. We should not call FreeLibrary()
            Dxcompiler::Instance().Terminate();
        }
    }

    return result;
}
#endif
