#include "pch.h"

namespace apn::auto_ducker
{
	//
	// プラグインの名前と情報です。
	//
	const auto plugin_name = L"自動ダッキング";
	const auto plugin_information = L"🐍自動ダッキング🔖r1";

	//
	// フィルタ項目の定義です。
	//
	namespace filter_item
	{
		//
		// フィルタ項目です。
		//
		FILTER_ITEM_TRACK volumes[2] = {
			{ L"音量L (%)", 0, 0, 100 },
			{ L"音量R (%)", 0, 0, 100 },
		};
		auto nb_exclude_layers = FILTER_ITEM_TRACK(L"除外レイヤー数", 0, 0, 100);
		auto nb_target_layers = FILTER_ITEM_TRACK(L"対象レイヤー数", 1, 0, 100);
		auto fadein = FILTER_ITEM_TRACK(L"開始フェード (秒)", 0, 0, 10, 0.1);
		auto fadeout = FILTER_ITEM_TRACK(L"終了フェード (秒)", 0, 0, 10, 0.1);
		auto blank_start = FILTER_ITEM_TRACK(L"開始延長 (秒)", 0, -10, 10, 0.1);
		auto blank_end = FILTER_ITEM_TRACK(L"終了延長 (秒)", 0, -10, 10, 0.1);
		auto flag_audio_buffer = FILTER_ITEM_CHECK(L"オーディオバッファ", false);

		//
		// フィルタ項目の配列です。
		//
		void* array[] =
		{
			&volumes[0], &volumes[1],
			&nb_exclude_layers, &nb_target_layers,
			&fadein, &fadeout,
			&blank_start, &blank_end,
//			&flag_audio_buffer,
			nullptr,
		};
	}

	//
	// 指定されたオブジェクトが音声オブジェクトの場合はTRUEを返します。
	//
	BOOL is_audio_object(OBJECT_HANDLE object)
	{
		// 1. 汎用プラグインのRegisterPlugin()でEDIT_HANDLEを取得しておく。
		// 1. 汎用プラグインのRegisterPlugin()以降でEDIT_HANDLE::enum_effect_name()を呼び出す。
		// 1. EFFECT_FLAG_AUDIOを持つ全ての音声エフェクト名を取得して配列に格納しておく。
		// 1. フィルタプラグインからアクセスできるようにエクスポート関数(is_audio_object)を実装する。
		// 1. エクスポート関数でobjectに対して全ての音声エフェクト名をEDIT_SECTION::find_effect()する。
		// 1. 音声エフェクトが1つでも見つかったらTRUEを返す。

		// 現状では判別が難しいので省略しています。
		return TRUE;
	}

	//
	// ゲインを取得します。
	//
	std::optional<float> get_gain(FILTER_PROC_AUDIO* audio)
	{
		MY_TRACE_FUNC("");

		// 戻り値のゲインです。
		std::optional<float> gain;

		// フレームレートが無効の場合は何もしません。
		if (audio->scene->rate <= 0 || audio->scene->scale <= 0) return gain;

		// 秒をフレームに変換する倍率を算出します。
		auto sec_to_frame =  1.0f * audio->scene->rate / audio->scene->scale;

		// フレームを秒に変換する倍率を算出します。
//		auto frame_to_sec =  1.0f / audio->scene->rate * audio->scene->scale;

		// 各フィルタ項目をフレーム単位で取得します。
		auto fadein = (int)(filter_item::fadein.value * sec_to_frame);
		auto fadeout = (int)(filter_item::fadeout.value * sec_to_frame);
		auto blank_start = (int)(filter_item::blank_start.value * sec_to_frame);
		auto blank_end = (int)(filter_item::blank_end.value * sec_to_frame);

		// 編集情報は取得できません。
		// ※audio->edit->infoはnullptrなので使用できません。
		// ※フィルタプラグインではEDIT_HANDLEも取得できません。
		// レイヤーが存在するかどうかを確認するAPIも存在しません。
		// よって、最大レイヤー数は取得できません。

		// 現在フレームを取得します。
		auto current_frame = audio->object->origin_frame;

		// 除外レイヤー数を取得します。
		auto nb_exclude_layers = (int)filter_item::nb_exclude_layers.value;

		// 対象レイヤー数を取得します。
		auto nb_target_layers = (int)filter_item::nb_target_layers.value;

		// 検索開始レイヤーです。
		auto find_layer_start = audio->object->layer + nb_exclude_layers + 1;

		// 検索終了レイヤーです。
		auto find_layer_end = find_layer_start + nb_target_layers;

		// 検索範囲のレイヤーを走査します。
		for(auto layer = find_layer_start; layer < find_layer_end; layer++)
		{
			// レイヤーが無効の場合は何もしません。
			if (!audio->edit->get_layer_enable(layer)) continue;

			// 検索位置です。
			auto frame = audio->object->frame_s - blank_start - fadein;

			// オブジェクトを検索します。
			while (auto object = audio->edit->find_object(layer, frame))
			{
				// タイムライン上でのオブジェクトの位置を取得します。
				auto object_layer_frame = audio->edit->get_object_layer_frame(object);

				// 検索位置を次に進めます。
				frame = object_layer_frame.end + 1;

				// 音声オブジェクトではない場合は除外します。
				if (!is_audio_object(object)) continue;

				//
				// この関数はゲインをセットします。
				//
				const auto set_gain = [&gain](float value)
				{
					gain = gain ? std::max(gain.value(), value) : value;
				};

				// fade_start extended_start オブジェクト extended_end fade_end

				// 開始位置と終了位置を延長した位置を算出します。
				const auto extended_start = object_layer_frame.start - blank_start;
				const auto extended_end = object_layer_frame.end + blank_end;

				// フェード位置を算出します。
				const auto fade_start = extended_start - fadein;
				const auto fade_end = extended_end + fadeout;

				// 現在フレームがダッキング範囲内の場合は
				if (fade_start <= current_frame && current_frame <= fade_end)
				{
					// 現在フレームが開始フェード内の場合は
					if (fade_start <= current_frame && current_frame < extended_start)
					{
						// ゲインをセットします。
						set_gain((float)(current_frame - fade_start) / fadein);
					}
					// 現在フレームが終了フェード内の場合は
					else if (extended_end <  current_frame && current_frame <= fade_end)
					{
						// ゲインをセットします。
						set_gain((float)(fade_end - current_frame) / fadeout);
					}
					// それ以外の場合は
					else
					{
						// ゲインを最大にします。
						gain = 1.0f;
					}
				}
			}
		}

		// ゲインを返します。
		return gain;
	}

	//
	// 音量をゲインします。
	// ゲインは「0.0f～1.0f」の範囲です。
	// 音量倍率は「1.0f～ユーザーが指定した倍率」にマップされます。
	//
	BOOL gain_volume(FILTER_PROC_AUDIO* audio, float gain)
	{
		MY_TRACE_FUNC("{/}", gain);
#if 1
		// パラメータをゲインします。
		audio->param->vol_l *= std::lerp(1.0f, (float)filter_item::volumes[0].value / 100, gain);
		audio->param->vol_r *= std::lerp(1.0f, (float)filter_item::volumes[1].value / 100, gain);
#else
		// サンプルをゲインします。

		// サンプルバッファを確保します。
		auto samples = std::make_unique<float[]>(audio->object->sample_num);

		// 音声チャンネルを走査します。
		for (auto channel = 0; channel < audio->object->channel_num; channel++)
		{
			// 音量が100%が指定されている場合は
			// 音量倍率が必ず1.0fになるので、何もしません。
			if (filter_item::volumes[channel].value == 100) continue;

			// 音量倍率を算出します。
			auto scale = std::lerp(1.0f, (float)filter_item::volumes[channel].value / 100, gain);

			// 元のサンプルを取得します。
			if (filter_item::flag_audio_buffer.value)
				audio->get_audiobuffer_data(samples.get(), channel);
			else
				audio->get_sample_data(samples.get(), channel);

			// 元のサンプルに音量倍率を乗算します。
			for (auto i = 0; i < audio->object->sample_num; i++) samples[i] *= scale;

			// サンプルをセットします。
			if (filter_item::flag_audio_buffer.value)
				audio->set_audiobuffer_data(samples.get(), channel);
			else
				audio->set_sample_data(samples.get(), channel);
		}
#endif
		return TRUE;
	}

	//
	// フィルタの音声処理です。
	//
	bool func_proc_audio(FILTER_PROC_AUDIO* audio)
	{
		// ゲインを取得します。
		if (auto gain = get_gain(audio))
		{
			// 音量をゲインします。
			gain_volume(audio, gain.value());
		}

		return true;
	}

	//
	// コンフィグ初期化関数です。
	//
	EXTERN_C void InitializeConfig(CONFIG_HANDLE* config)
	{
	}

	//
	// フィルタ登録関数です。
	//
	EXTERN_C FILTER_PLUGIN_TABLE* GetFilterPluginTable()
	{
		static FILTER_PLUGIN_TABLE filter_plugin_table =
		{
			FILTER_PLUGIN_TABLE::FLAG_AUDIO |
			FILTER_PLUGIN_TABLE::FLAG_FILTER |
			0,
			plugin_name,
			L"音声効果",
			plugin_information,
			filter_item::array,
			nullptr,
			func_proc_audio
		};

		return &filter_plugin_table;
	}

	//
	// エントリポイントです。
	//
	EXTERN_C BOOL APIENTRY DllMain(HMODULE instance, DWORD reason, LPVOID reserved)
	{
		switch (reason)
		{
		case DLL_PROCESS_ATTACH:
			{
				// サブスレッドが起動する度にこのDllMain()が呼ばれるのを防ぎます。
				::DisableThreadLibraryCalls(instance);

				break;
			}
		case DLL_PROCESS_DETACH:
			{
				break;
			}
		}

		return TRUE;
	}
}
