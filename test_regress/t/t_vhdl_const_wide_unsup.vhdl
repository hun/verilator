-- DESCRIPTION: Verilator: a constant too wide to fold
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A memory image written as one constant is legal VHDL, but the frontend binds a
-- constant to a value and rebuilds it per element write, which costs the value's
-- width every time. neo430's boot ROM is exactly this, so the constant is reported
-- instead of folded.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_const_wide_unsup is
  port (
    o : out std_logic
  );
end entity;

architecture rtl of t_vhdl_const_wide_unsup is
  constant image : std_logic_vector(2097151 downto 0) := (others => '0');
begin
  o <= image(3);
end architecture;