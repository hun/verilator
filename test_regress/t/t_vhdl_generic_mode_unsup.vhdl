-- DESCRIPTION: Verilator: VHDL generic with mode out is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
--
-- Generics take only the in mode. The mode out is rejected by the native VHDL frontend as unsupported.

entity t is
  generic (n : out integer);
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
end architecture rtl;
