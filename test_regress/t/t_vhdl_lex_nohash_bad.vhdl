-- DESCRIPTION: Verilator: VHDL based literal diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A based literal without its closing hash is a lexical error.

entity top is
end entity;

architecture rtl of top is
  constant c : integer := 16#FF;
begin
end architecture;
