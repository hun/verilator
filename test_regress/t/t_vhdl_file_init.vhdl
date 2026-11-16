-- DESCRIPTION: Verilator: a std.textio file builds a memory image at elaboration
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity t is
  port (
    o0 : out std_logic_vector(7 downto 0);
    o1 : out std_logic_vector(7 downto 0)
  );
end entity t;

architecture rtl of t is
  type ram_t is array (0 to 1) of std_logic_vector(7 downto 0);

  -- hread fills as many hex digits as the target is wide, so the "01" of the
  -- second line is left unread
  impure function init_ram(name : string) return ram_t is
    file ram_file : text open read_mode is name;
    variable ram_line : line;
    variable temp_word : std_logic_vector(7 downto 0);
    variable temp_ram : ram_t := (others => (others => '0'));
  begin
    for i in 0 to 1 loop
      exit when endfile(ram_file);
      readline(ram_file, ram_line);
      hread(ram_line, temp_word);
      temp_ram(i) := temp_word;
    end loop;
    return temp_ram;
  end function init_ram;

  -- The image is read while the design is elaborated, not while it runs
  signal mem : ram_t := init_ram("t/t_vhdl_file_init.txt");
begin
  o0 <= mem(0);
  o1 <= mem(1);
end architecture rtl;
