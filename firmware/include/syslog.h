// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023
/// @file

#ifndef SYSLOG_H_
#define SYSLOG_H_

#include <deque>
#include <string_view>
#include <cstddef>
#include <functional>
#include <format>

#include <sys/time.h>

template<size_t max_size>
class syslog
{
public:
	void push(std::string_view str)
	{
		if (str.size() > max_size)
			return; // FIXME return some kind of error?

		while (logs_.size() && str.size() > space_available_)
		{
			space_available_ += logs_.front().record.size();
			logs_.pop_front();
		}

		space_available_ -= str.size();
		timeval tm;
		gettimeofday(&tm, nullptr);
		logs_.emplace_back(std::string(str), std::move(tm));

		if (callback_)
		{
			callback_(str);
		}
	}

	size_t size() const
	{
		return logs_.size();
	}

	size_t bytes() const
	{
		return max_size - space_available_;
	}

	std::string operator[](size_t index) const
	{
		auto& log = logs_[index];
		return std::format("{}.{:0^6} - {}", log.time.tv_sec, log.time.tv_usec, log.record);
	}

	std::string_view back() const
	{
		return logs_.back();
	}

	template<class Func, class... Args>
	void register_push_callback(Func&& func, Args&&... args)
	{
		auto wrapper = [func, ... args = std::forward<Args>(args)](std::string_view str)
		{
			func(std::forward<Args>(args)..., str);
		};
		callback_ = wrapper;
	}

private:
	struct log {
		std::string record;
		timeval time;
	};
	size_t space_available_ = max_size;
	std::deque<log> logs_;
	std::function<void(std::string_view)> callback_;
};

#include <FreeRTOS.h>
#include <semphr.h>

template<class syslog>
class safe_syslog
{
public:
	safe_syslog()
	{
		mutex_ = xSemaphoreCreateBinaryStatic(&mutex_buffer_);
		xSemaphoreGive(mutex_);
	}

	void push(std::string_view&& str)
	{
		xSemaphoreTake(mutex_, portMAX_DELAY);
		log_.push(str);
		xSemaphoreGive(mutex_);
	}

	size_t size() const
	{
		xSemaphoreTake(mutex_, portMAX_DELAY);
		size_t result = log_.size();
		xSemaphoreGive(mutex_);
		return result;
	}

	size_t bytes() const
	{
		xSemaphoreTake(mutex_, portMAX_DELAY);
		size_t result = log_.bytes_();
		xSemaphoreGive(mutex_);
		return result;
	}

	std::string operator[](size_t index) const
	{
		xSemaphoreTake(mutex_, portMAX_DELAY);
		std::string result = std::string(log_[index]);
		xSemaphoreGive(mutex_);
		return result;
	}

	std::string back() const
	{
		xSemaphoreTake(mutex_, portMAX_DELAY);
		std::string result = log_.back();
		xSemaphoreGive(mutex_);
		return result;
	}

	template<class Func, class... Args>
	void register_push_callback(Func&& func, Args&&... args)
	{
		xSemaphoreTake(mutex_, portMAX_DELAY);
		log_.register_push_callback(std::forward<Func>(func), std::forward<Args>(args)...);
		xSemaphoreGive(mutex_);
	}

private:
	syslog log_;
	StaticSemaphore_t mutex_buffer_;
	SemaphoreHandle_t mutex_;
};

#endif//SYSLOG_H_
